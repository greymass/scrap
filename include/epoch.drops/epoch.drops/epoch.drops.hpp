#pragma once

#include <cmath>
#include <drops/drops.hpp>
#include <eosio.system/eosio.system.hpp>

using namespace eosio;
using namespace std;

static constexpr name drops_contract = "seed.gm"_n; // location of drops contract

static const string ERROR_SYSTEM_DISABLED = "Drops system is disabled.";

namespace dropssystem {

class [[eosio::contract("epoch.drops")]] epoch : public contract
{
public:
   using contract::contract;

   struct [[eosio::table("commit")]] commit_row
   {
      uint64_t    id;
      uint64_t    epoch;
      name        oracle;
      checksum256 commit;
      uint64_t    primary_key() const { return id; }
      uint64_t    by_epoch() const { return epoch; }
      uint128_t   by_epochoracle() const { return ((uint128_t)oracle.value << 64) | epoch; }
   };

   struct [[eosio::table("epoch")]] epoch_row
   {
      uint64_t     epoch;
      vector<name> oracles;
      checksum256  seed;
      uint64_t     primary_key() const { return epoch; }
   };

   struct [[eosio::table("oracle")]] oracle_row
   {
      name     oracle;
      uint64_t primary_key() const { return oracle.value; }
   };

   struct [[eosio::table("reveal")]] reveal_row
   {
      uint64_t  id;
      uint64_t  epoch;
      name      oracle;
      string    reveal;
      uint64_t  primary_key() const { return id; }
      uint64_t  by_epoch() const { return epoch; }
      uint128_t by_epochoracle() const { return ((uint128_t)oracle.value << 64) | epoch; }
   };

   struct [[eosio::table("state")]] state_row
   {
      block_timestamp genesis  = current_block_time();
      uint32_t        duration = 86400; // Epoch duration, 1-day default
      bool            enabled  = false;
   };

   typedef eosio::multi_index<"epoch"_n, epoch_row> epoch_table;
   typedef eosio::multi_index<
      "commit"_n,
      commit_row,
      eosio::indexed_by<"epoch"_n, eosio::const_mem_fun<commit_row, uint64_t, &commit_row::by_epoch>>,
      eosio::indexed_by<"epochoracle"_n, eosio::const_mem_fun<commit_row, uint128_t, &commit_row::by_epochoracle>>>
                                                      commit_table;
   typedef eosio::multi_index<"oracle"_n, oracle_row> oracle_table;
   typedef eosio::multi_index<
      "reveal"_n,
      reveal_row,
      eosio::indexed_by<"epoch"_n, eosio::const_mem_fun<reveal_row, uint64_t, &reveal_row::by_epoch>>,
      eosio::indexed_by<"epochoracle"_n, eosio::const_mem_fun<reveal_row, uint128_t, &reveal_row::by_epochoracle>>>
                                                  reveal_table;
   typedef eosio::singleton<"state"_n, state_row> state_table;

   /*
    Oracle actions
   */
   [[eosio::action]] void commit(const name oracle, const uint64_t epoch, const checksum256 commit);
   using commit_action = eosio::action_wrapper<"commit"_n, &epoch::commit>;

   [[eosio::action]] void reveal(const name oracle, const uint64_t epoch, const string reveal);
   using reveal_action = eosio::action_wrapper<"reveal"_n, &epoch::reveal>;

   [[eosio::action]] void forcereveal(const uint64_t epoch, const string salt);
   using forcereveal_action = eosio::action_wrapper<"forcereveal"_n, &epoch::forcereveal>;

   [[eosio::action, eosio::read_only]] checksum256 computehash(const uint64_t epoch, const vector<string> reveals);
   using computehash_action = eosio::action_wrapper<"computehash"_n, &epoch::computehash>;

   [[eosio::action, eosio::read_only]] uint64_t getepoch();
   using getepoch_action = eosio::action_wrapper<"getepoch"_n, &epoch::getepoch>;

   [[eosio::action, eosio::read_only]] vector<name> getoracles();
   using getoracles_action = eosio::action_wrapper<"getoracles"_n, &epoch::getoracles>;

   /*
    Admin actions
   */
   [[eosio::action]] void addoracle(const name oracle);
   using addoracle_action = eosio::action_wrapper<"addoracle"_n, &epoch::addoracle>;

   [[eosio::action]] void removeoracle(const name oracle);
   using removeoracle_action = eosio::action_wrapper<"removeoracle"_n, &epoch::removeoracle>;

   [[eosio::action]] void init();
   using init_action = eosio::action_wrapper<"init"_n, &epoch::init>;

   [[eosio::action]] void enable(const bool enabled);
   using enable_action = eosio::action_wrapper<"enable"_n, &epoch::enable>;

   [[eosio::action]] void duration(const uint32_t duration);
   using duration_action = eosio::action_wrapper<"duration"_n, &epoch::duration>;

   [[eosio::action]] epoch_row advance();
   using advance_action = eosio::action_wrapper<"advance"_n, &epoch::advance>;

   /*
    Computation helpers
   */
   static constexpr char hexmap[] = {'0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'a', 'b', 'c', 'd', 'e', 'f'};

   static uint64_t derive_epoch(const block_timestamp genesis, const uint32_t duration)
   {
      return floor((current_time_point().sec_since_epoch() - genesis.to_time_point().sec_since_epoch()) / duration) + 1;
   }

   static string hex_to_str(const unsigned char* data, const int len)
   {
      string s(len * 2, ' ');
      for (int i = 0; i < len; ++i) {
         s[2 * i]     = hexmap[(data[i] & 0xF0) >> 4];
         s[2 * i + 1] = hexmap[data[i] & 0x0F];
      }
      return s;
   }

   static string checksum256_to_string(const checksum256& checksum)
   {
      auto byte_array = checksum.extract_as_byte_array();
      return hex_to_str(byte_array.data(), byte_array.size());
   }

   static uint16_t clzhex(const std::string& hexString)
   {
      int  count        = 0;
      bool foundNonZero = false;

      for (char c : hexString) {
         if (c == '0' && !foundNonZero) {
            count++;
         } else {
            foundNonZero = true;
            break;
         }
      }

      return count;
   }

   static uint16_t clzbinary(const checksum256 checksum)
   {
      auto                 byte_array    = checksum.extract_as_byte_array();
      const uint8_t*       my_bytes      = (uint8_t*)byte_array.data();
      size_t               size          = byte_array.size();
      size_t               lzbits        = 0;
      static const uint8_t char2lzbits[] = {
         // 0  1  2  3  4  5  6  7  8  9 10 11 12 13 14 15 16 17 18 19 20 21 22
         // 23 24 25 26 27 28 29 30 31
         8, 7, 6, 6, 5, 5, 5, 5, 4, 4, 4, 4, 4, 4, 4, 4, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 2, 2, 2, 2, 2,
         2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
         1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
         1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
         0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
         0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
         0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};

      size_t i = 0;

      while (true) {
         uint8_t c = my_bytes[i];
         lzbits += char2lzbits[c];
         if (c != 0)
            break;
         ++i;
         if (i >= size)
            return 0x100;
      }

      return lzbits;
   }

   static checksum256 hash(const checksum256 epochseed, const string data)
   {
      string result = checksum256_to_string(epochseed) + data;
      return sha256(result.c_str(), result.length());
   }

   static checksum256 hashdrop(const checksum256 epochseed, const uint64_t drops_id)
   {
      return hash(epochseed, to_string(drops_id));
   }

   static checksum256 hashdrops(const checksum256 epochseed, const vector<uint64_t> drops_ids)
   {
      string data = "";
      for (const auto& id : drops_ids)
         data += to_string(id);

      return hash(epochseed, data);
   }

// DEBUG (used to help testing)
#ifdef DEBUG
   [[eosio::action]] void test(const string data);

   // @debug
   [[eosio::action]] void
   cleartable(const name table_name, const optional<name> scope, const optional<uint64_t> max_rows);

   [[eosio::action]] void wipe();
   using wipe_action = eosio::action_wrapper<"wipe"_n, &epoch::wipe>;

#endif

private:
   void check_is_enabled();

   epoch::epoch_row    advance_epoch();
   void                ensure_epoch_advance(const uint64_t epoch);
   void                ensure_epoch_reveal(const uint64_t epoch);
   void                cleanup_epoch(const uint64_t epoch, const vector<name> oracles);
   void                remove_oracle_commit(const uint64_t epoch, const name oracle);
   void                remove_oracle_reveal(const uint64_t epoch, const name oracle);
   bool                oracle_has_committed(const name oracle, const uint64_t epoch);
   bool                oracle_has_revealed(const name oracle, const uint64_t epoch);
   void                complete_epoch(const uint64_t epoch, const checksum256 epoch_seed);
   vector<name>        get_active_oracles();
   vector<string>      get_epoch_reveals(const uint64_t epoch);
   vector<checksum256> get_epoch_commits(const uint64_t epoch);
   uint64_t            get_current_epoch_height();
   epoch_row           get_epoch(const uint64_t epoch);
   reveal_row          get_reveal(const name oracle, const uint64_t epoch);
   commit_row          get_commit(name const oracle, const uint64_t epoch);

   void emplace_commit(const uint64_t epoch, const name oracle, const checksum256 commit);
   void emplace_reveal(const uint64_t epoch, const name oracle, const string reveal);
// DEBUG (used to help testing)
#ifdef DEBUG
   template <typename T>
   void clear_table(T& table, uint64_t rows_to_clear);
#endif
};

} // namespace dropssystem
