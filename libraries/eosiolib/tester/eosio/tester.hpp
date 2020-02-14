#pragma once
#define FMT_HEADER_ONLY

#include <eosio/asset.hpp>
#include <eosio/chain_types.hpp>
#include <eosio/crypto.hpp>
#include <eosio/producer_schedule.hpp>
#include <eosio/database.hpp>
#include <eosio/eosio.hpp>
#include <eosio/transaction.hpp>
#include <eosio/from_string.hpp>
#include <cwchar>

#include <fmt/format.h>

#define TESTER_INTRINSIC extern "C" __attribute__((eosio_wasm_import))

namespace eosio {
namespace internal_use_do_not_use {

   TESTER_INTRINSIC void     query_database_chain(uint32_t chain, void* req_begin, void* req_end, void* cb_alloc_data,
                                                  void* (*cb_alloc)(void* cb_alloc_data, size_t size));

   template <typename T, typename Alloc_fn>
   inline void query_database_chain(uint32_t chain, const T& req, Alloc_fn alloc_fn) {
      auto req_data = pack(req);
      query_database_chain(chain, req_data.data(), req_data.data() + req_data.size(), &alloc_fn,
                           [](void* cb_alloc_data, size_t size) -> void* {
                              return (*reinterpret_cast<Alloc_fn*>(cb_alloc_data))(size);
                           });
   }

} // namespace internal_use_do_not_use

const std::vector<std::string>& get_args();

std::vector<char> read_whole_file(std::string_view filename);

int32_t execute(std::string_view command);

asset string_to_asset(const char* s);

inline asset  s2a(const char* s) { return string_to_asset(s); }

using chain_types::transaction_status;

auto conversion_kind(chain_types::permission_level, permission_level) -> strict_conversion;
auto conversion_kind(chain_types::action, action) -> strict_conversion;

using chain_types::account_delta;
using chain_types::account_auth_sequence;

struct action_receipt {
   name                               receiver        = {};
   checksum256                        act_digest      = {};
   uint64_t                           global_sequence = {};
   uint64_t                           recv_sequence   = {};
   std::vector<account_auth_sequence> auth_sequence   = {};
   uint32_t                           code_sequence   = {};
   uint32_t                           abi_sequence    = {};
};

auto conversion_kind(chain_types::action_receipt_v0, action_receipt) -> strict_conversion;

struct action_trace {
   uint32_t                      action_ordinal         = {};
   uint32_t                      creator_action_ordinal = {};
   std::optional<action_receipt> receipt                = {};
   name                          receiver               = {};
   action                        act                    = {};
   bool                          context_free           = {};
   int64_t                       elapsed                = {};
   std::string                   console                = {};
   std::vector<account_delta>    account_ram_deltas     = {};
   std::optional<std::string>    except                 = {};
   std::optional<uint64_t>       error_code             = {};
};

auto conversion_kind(chain_types::action_trace_v0, action_trace) -> strict_conversion;

struct transaction_trace {
   checksum256                            id                  = {};
   transaction_status                     status              = {};
   uint32_t                               cpu_usage_us        = {};
   uint32_t                               net_usage_words     = {};
   int64_t                                elapsed             = {};
   uint64_t                               net_usage           = {};
   bool                                   scheduled           = {};
   std::vector<action_trace>              action_traces       = {};
   std::optional<account_delta>           account_ram_delta   = {};
   std::optional<std::string>             except              = {};
   std::optional<uint64_t>                error_code          = {};
   std::vector<transaction_trace>         failed_dtrx_trace   = {};
};

auto conversion_kind(chain_types::transaction_trace_v0, transaction_trace) -> narrowing_conversion;
auto serialize_as(const transaction_trace&) -> chain_types::transaction_trace;

using chain_types::block_info;

/**
 * Validates the status of a transaction.  If expected_except is nullptr, then the
 * transaction should succeed.  Otherwise it represents a string which should be
 * part of the error message.
 */
void expect(const transaction_trace& tt, const char* expected_except = nullptr);

template <typename SrcIt, typename DestIt>
void hex(SrcIt begin, SrcIt end, DestIt dest) {
    auto nibble = [&dest](uint8_t i) {
        if (i <= 9)
            *dest++ = '0' + i;
        else
            *dest++ = 'A' + i - 10;
    };
    while (begin != end) {
        nibble(((uint8_t)*begin) >> 4);
        nibble(((uint8_t)*begin) & 0xf);
        ++begin;
    }
}

template<std::size_t Size>
std::ostream& operator<<(std::ostream& os, const fixed_bytes<Size>& d) {
   auto arr = d.extract_as_byte_array();
   hex(arr.begin(), arr.end(), std::ostreambuf_iterator<char>(os.rdbuf()));
   return os;
}

std::ostream& operator<<(std::ostream& os, const block_timestamp& obj);
std::ostream& operator<<(std::ostream& os, const name& obj);

/**
 * Manages a chain.
 * Only one test_chain can exist at a time.
 * The test chain uses simulated time starting at 2020-01-01T00:00:00.000.
 */
class test_chain {
 private:
   uint32_t                               id;
   std::optional<block_info>              head_block_info;

 public:
   public_key  default_pub_key  = public_key_from_string("EOS6MRyAjQq8ud7hVNYcfnVPJqcVpscN5So8BhtHuGYqET5GDW5CV").value();
   private_key default_priv_key = private_key_from_string("5KQwrPbwdL6PhXujxW37FSSQZ1JiwsST4cqQzDeyXtP79zkvFD3").value();

   test_chain();
   test_chain(const test_chain&) = delete;
   ~test_chain();

   test_chain& operator=(const test_chain&) = delete;

   /**
    * Start a new pending block.  If a block is currently pending, finishes it first.
    * May push additional blocks if any time is skipped.
    *
    * @param skip_milliseconds The amount of time to skip in addition to the 500 ms block time.
    * truncated to a multiple of 500 ms.
    */
   void start_block(int64_t skip_miliseconds = 0);

   /**
    * Finish the current pending block.  If no block is pending, creates an empty block.
    */
   void finish_block();

   const block_info& get_head_block_info();

   void fill_tapos(transaction& t, uint32_t expire_sec = 1);

   transaction make_transaction();
   transaction make_transaction(std::vector<action>&& actions);

   /**
    * Pushes a transaction onto the chain.  If no block is currently pending, starts one.
    */
   [[nodiscard]]
   transaction_trace push_transaction(const transaction& trx, const std::vector<private_key>& keys,
                                      const std::vector<std::vector<char>>& context_free_data = {},
                                      const std::vector<signature>& signatures        = {});

   [[nodiscard]]
   transaction_trace push_transaction(const transaction& trx);

   /**
    * Pushes a transaction onto the chain.  If no block is currently pending, starts one.
    *
    * Validates the transaction status according to @ref eosio::expect.
    */
   transaction_trace transact(std::vector<action>&& actions, const std::vector<private_key>& keys,
                              const char* expected_except = nullptr);

   transaction_trace transact(std::vector<action>&& actions, const char* expected_except = nullptr);

   /**
    * Executes a single deferred transaction and returns the action trace.
    * If there are no available deferred transactions that are ready to execute
    * in the current pending block, returns an empty optional.
    *
    * If no block is currently pending, starts one.
    */
   [[nodiscard]]
   std::optional<transaction_trace> exec_deferred();

   transaction_trace create_account(name ac, const public_key& pub_key,
                                    const char* expected_except = nullptr);

   transaction_trace create_account(name ac, const char* expected_except = nullptr);

   /**
    * Create an account that can have a contract set on it.
    * @param account The name of the account
    * @param pub_key The public key to use for the account.  Defaults to default_pub_key.
    * @param is_priv Determines whether the contract should be privileged.  Defaults to false.
    * @param expected_except Used to validate the transaction status according to @ref eosio::expect.
    */
   transaction_trace create_code_account(name account, const public_key& pub_key, bool is_priv = false,
                                         const char* expected_except = nullptr);
   transaction_trace create_code_account(name ac, const public_key& pub_key, const char* expected_except);
   transaction_trace create_code_account(name ac, bool is_priv = false,
                                         const char* expected_except = nullptr);
   transaction_trace create_code_account(name ac, const char* expected_except);

   /*
    * Set the code for an account.
    * Validates the transaction status as with @ref eosio::expect.
    */
   transaction_trace set_code(name ac, const char* filename, const char* expected_except = nullptr);

   /**
    * Creates a new token.
    * The eosio.token contract should be deployed on the @c contract account.
    */
   transaction_trace create_token(name contract, name signer, name issuer, asset maxsupply,
                                  const char* expected_except = nullptr);

   transaction_trace issue(const name& contract, const name& issuer, const asset& amount,
                           const char* expected_except = nullptr);

   transaction_trace transfer(const name& contract, const name& from, const name& to, const asset& amount,
                              const std::string& memo = "", const char* expected_except = nullptr);

   transaction_trace issue_and_transfer(const name& contract, const name& issuer, const name& to,
                                        const asset& amount, const std::string& memo = "",
                                        const char* expected_except = nullptr);

   template <typename T>
   inline std::vector<char> query_database(const T& request) {
      std::vector<char> result;
      internal_use_do_not_use::query_database_chain(id, request, [&result](size_t size) {
         result.resize(size);
         return result.data();
      });
      return result;
   }
};

} // namespace eosio

namespace chain_types {

std::ostream& operator<<(std::ostream& os, transaction_status t);
std::ostream& operator<<(std::ostream& os, const account_auth_sequence& aas);
std::ostream& operator<<(std::ostream& os, const account_delta& ad);

}
