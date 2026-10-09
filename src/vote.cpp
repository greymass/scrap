namespace eosio {

void token::init(const name           eon,
                 const name           nex,
                 const time_point_sec epoch_start,
                 const uint32_t       epoch_seconds,
                 const uint32_t       notice_epochs)
{
   require_auth(get_self());

   config_table config(get_self(), get_self().value);
   check(!config.exists(), "The handover is already initialized.");
   check(is_account(eon), "eon account does not exist");
   check(is_account(nex), "nex account does not exist");
   check(epoch_seconds > 0, "epoch_seconds must be positive");
   check(notice_epochs > 0, "notice_epochs must be positive");
   get_supply(get_self(), SCRAP_SYMBOL.code());

   config.set(config_row{eon, nex, epoch_start, epoch_seconds, notice_epochs}, get_self());

   vote_table votes(get_self(), get_self().value);
   vote_row   vote;
   vote.tally  = asset{0, SCRAP_SYMBOL};
   vote.supply = asset{0, SCRAP_SYMBOL};
   votes.set(vote, get_self());
}

void token::approve(const name holder)
{
   require_auth(holder);

   vote_table votes(get_self(), get_self().value);
   check(votes.exists(), "The vote has not opened.");
   auto vote = votes.get();
   check(!vote.ratified, "The vote has passed and approvals are final.");

   approvals_table approvals(get_self(), get_self().value);
   check(approvals.find(holder.value) == approvals.end(), "This account already approves.");
   approvals.emplace(holder, [&](auto& row) {
      row.account  = holder;
      row.snapshot = asset{0, SCRAP_SYMBOL};
   });

   vote.tally.amount += balance_of(holder);
   try_ratify(vote);
   votes.set(vote, get_self());
}

void token::withdraw(const name holder)
{
   require_auth(holder);

   vote_table votes(get_self(), get_self().value);
   check(votes.exists(), "The vote has not opened.");
   auto vote = votes.get();
   check(!vote.ratified, "The vote has passed and approvals are final.");

   approvals_table approvals(get_self(), get_self().value);
   const auto&     row = approvals.get(holder.value, "This account does not approve.");
   approvals.erase(row);

   vote.tally.amount -= balance_of(holder);
   votes.set(vote, get_self());
}

void token::closevote(const uint32_t max_rows)
{
   check(max_rows > 0, "max_rows must be positive");

   vote_table votes(get_self(), get_self().value);
   check(votes.exists(), "The vote has not opened.");
   auto vote = votes.get();
   check(vote.ratified, "The vote has not passed.");
   check(!vote.closed, "The vote is already closed.");

   approvals_table approvals(get_self(), get_self().value);
   vector<support> supporters;
   auto            itr = approvals.begin();
   for (uint32_t i = 0; i < max_rows && itr != approvals.end(); ++i) {
      const asset weight = itr->snapped ? itr->snapshot : asset{balance_of(itr->account), SCRAP_SYMBOL};
      supporters.push_back(support{itr->account, weight});
      itr = approvals.erase(itr);
   }

   if (itr == approvals.end()) {
      vote.closed = true;
      votes.set(vote, get_self());
   }

   token::logsupport_action logsupport{get_self(), {get_self(), "active"_n}};
   logsupport.send(supporters, vote.closed);
}

void token::logratify(const asset          tally,
                      const asset          supply,
                      const uint32_t       ratified_epoch,
                      const uint32_t       start_epoch,
                      const time_point_sec start_time)
{
   require_auth(get_self());
   config_table config(get_self(), get_self().value);
   require_recipient(config.get().eon);
}

void token::logsupport(const vector<support> supporters, const bool closed) { require_auth(get_self()); }

int64_t token::balance_of(const name& owner)
{
   accounts   acnts(get_self(), owner.value);
   const auto itr = acnts.find(SCRAP_SYMBOL.code().raw());
   return itr == acnts.end() ? 0 : itr->balance.amount;
}

uint32_t token::current_epoch(const config_row& config)
{
   const uint32_t now   = current_time_point().sec_since_epoch();
   const uint32_t start = config.epoch_start.sec_since_epoch();
   if (now < start) {
      return 0;
   }
   return (now - start) / config.epoch_seconds + 1;
}

bool token::handover_started()
{
   vote_table votes(get_self(), get_self().value);
   if (!votes.exists()) {
      return false;
   }
   const auto vote = votes.get();
   return vote.ratified && current_time_point().sec_since_epoch() >= vote.start_time.sec_since_epoch();
}

bool token::try_ratify(vote_row& vote)
{
   const asset supply = get_supply(get_self(), SCRAP_SYMBOL.code());
   if (vote.tally.amount <= supply.amount - vote.tally.amount) {
      return false;
   }

   config_table     config_tbl(get_self(), get_self().value);
   const config_row config = config_tbl.get();

   vote.ratified       = true;
   vote.supply         = supply;
   vote.ratified_epoch = current_epoch(config);
   vote.start_epoch    = vote.ratified_epoch + config.notice_epochs;
   vote.start_time     = time_point_sec(static_cast<uint32_t>(config.epoch_start.sec_since_epoch() +
                                                              uint64_t(vote.start_epoch - 1) * config.epoch_seconds));

   token::logratify_action logratify{get_self(), {get_self(), "active"_n}};
   logratify.send(vote.tally, vote.supply, vote.ratified_epoch, vote.start_epoch, vote.start_time);
   return true;
}

void token::track_vote(std::initializer_list<balance_change> changes, const bool supply_changed)
{
   vote_table votes(get_self(), get_self().value);
   if (!votes.exists()) {
      return;
   }
   auto vote = votes.get();
   if (vote.closed) {
      return;
   }

   approvals_table approvals(get_self(), get_self().value);
   int64_t         delta = 0;
   for (const auto& change : changes) {
      const auto itr = approvals.find(change.owner.value);
      if (itr == approvals.end()) {
         continue;
      }
      if (!vote.ratified) {
         delta += change.after - change.before;
      } else if (!itr->snapped) {
         approvals.modify(itr, same_payer, [&](auto& row) {
            row.snapshot = asset{change.before, SCRAP_SYMBOL};
            row.snapped  = true;
         });
      }
   }

   if (vote.ratified || (delta == 0 && !supply_changed)) {
      return;
   }

   vote.tally.amount += delta;
   if (try_ratify(vote) || delta != 0) {
      votes.set(vote, get_self());
   }
}

} // namespace eosio
