namespace eosio {

void token::setcurve(const vector<curve_band> bands, const uint32_t ramp_epochs)
{
   require_auth(get_self());

   curve_table curve(get_self(), get_self().value);
   check(curve.exists(), "The handover is not initialized.");
   check_curve(bands, ramp_epochs);
   curve.set(curve_row{bands, ramp_epochs}, get_self());
}

void token::commit(const uint32_t closed_epoch, const uint32_t opened_epoch, const asset amount)
{
   config_table   config_tbl(get_self(), get_self().value);
   vote_table     votes(get_self(), get_self().value);
   emission_table emission_tbl(get_self(), get_self().value);
   if (!config_tbl.exists() || !votes.exists() || !emission_tbl.exists()) {
      return;
   }

   const config_row config = config_tbl.get();
   check(get_sender() == config.eon, "Only the game commits SCRAP to Shares.");

   const vote_row vote = votes.get();
   if (!vote.ratified) {
      return;
   }

   stats      statstable(get_self(), SCRAP_SYMBOL.code().raw());
   const auto st = statstable.find(SCRAP_SYMBOL.code().raw());
   if (st == statstable.end()) {
      return;
   }

   auto           emission  = emission_tbl.get();
   const uint32_t now_epoch = current_epoch(config);

   if (closed_epoch == emission.target_epoch && closed_epoch > emission.committed_epoch && closed_epoch < now_epoch) {
      const int64_t room     = st->max_supply.amount - st->supply.amount - emission.owed.amount;
      int64_t       accepted = 0;
      if (amount.symbol == SCRAP_SYMBOL && amount.amount > 0) {
         accepted = std::min({amount.amount, emission.target.amount, std::max<int64_t>(room, 0)});
      }
      emission.owed.amount += accepted;
      emission.committed_epoch = closed_epoch;
      emission.committed       = asset{accepted, SCRAP_SYMBOL};
   }

   if (opened_epoch > closed_epoch && opened_epoch > emission.target_epoch && opened_epoch <= now_epoch) {
      emission.target_epoch = opened_epoch;
      emission.target       = asset{epoch_target(vote, opened_epoch, emission.owed.amount), SCRAP_SYMBOL};

      token::logtarget_action logtarget{get_self(), {get_self(), "active"_n}};
      logtarget.send(closed_epoch, opened_epoch, emission.target);
   }

   emission_tbl.set(emission, get_self());
}

void token::logtarget(const uint32_t closed_epoch, const uint32_t opened_epoch, const asset target)
{
   require_auth(get_self());
   config_table config(get_self(), get_self().value);
   require_recipient(config.get().eon);
}

void token::mintshare(const name player, const asset quantity)
{
   config_table config_tbl(get_self(), get_self().value);
   check(config_tbl.exists(), "The handover is not initialized.");
   const config_row config = config_tbl.get();
   check(get_sender() == config.eon, "Only the game mints Shares.");

   check(quantity.is_valid(), "invalid quantity");
   check(quantity.symbol == SCRAP_SYMBOL, "symbol precision mismatch");
   check(quantity.amount > 0, "must mint positive quantity");

   emission_table emission_tbl(get_self(), get_self().value);
   auto           emission = emission_tbl.get();
   check(quantity.amount <= emission.owed.amount, "The Share exceeds the SCRAP owed to Shares.");
   emission.owed -= quantity;
   emission_tbl.set(emission, get_self());

   stats       statstable(get_self(), SCRAP_SYMBOL.code().raw());
   const auto& st = statstable.get(SCRAP_SYMBOL.code().raw(), "token with symbol does not exist");
   statstable.modify(st, same_payer, [&](auto& s) { s.supply += quantity; });

   const int64_t before = add_balance(config.nex, quantity, get_self());
   require_recipient(config.nex);
   track_vote({{config.nex, before, before + quantity.amount}}, true);
}

void token::check_curve(const vector<curve_band>& bands, const uint32_t ramp_epochs)
{
   check(ramp_epochs > 0, "ramp_epochs must be positive");
   check(!bands.empty(), "The curve needs at least one band.");

   const asset max_supply = stats(get_self(), SCRAP_SYMBOL.code().raw()).get(SCRAP_SYMBOL.code().raw()).max_supply;
   int64_t     below      = 0;
   for (const auto& band : bands) {
      check(band.below.symbol == SCRAP_SYMBOL && band.per_epoch.symbol == SCRAP_SYMBOL, "symbol precision mismatch");
      check(band.below.amount > below, "Band edges must increase.");
      check(band.below.amount <= max_supply.amount, "A band edge exceeds the maximum supply.");
      check(band.per_epoch.amount >= 0 && band.per_epoch.amount <= max_supply.amount, "A band target is out of range.");
      below = band.below.amount;
   }
}

int64_t token::owed_amount()
{
   emission_table emission(get_self(), get_self().value);
   return emission.exists() ? emission.get().owed.amount : 0;
}

int64_t token::epoch_target(const vote_row& vote, const uint32_t epoch, const int64_t owed)
{
   if (!vote.ratified || epoch < vote.start_epoch) {
      return 0;
   }

   curve_table curve_tbl(get_self(), get_self().value);
   if (!curve_tbl.exists()) {
      return 0;
   }
   const curve_row curve = curve_tbl.get();

   stats      statstable(get_self(), SCRAP_SYMBOL.code().raw());
   const auto st = statstable.find(SCRAP_SYMBOL.code().raw());
   if (st == statstable.end()) {
      return 0;
   }

   const int64_t issued = st->supply.amount + owed;
   if (issued >= st->max_supply.amount) {
      return 0;
   }

   int64_t per_epoch = 0;
   for (const auto& band : curve.bands) {
      if (issued < band.below.amount) {
         per_epoch = band.per_epoch.amount;
         break;
      }
   }

   const uint64_t step = uint64_t(epoch - vote.start_epoch) + 1;
   if (step < curve.ramp_epochs) {
      per_epoch = static_cast<int64_t>(uint64_t(per_epoch) * step / curve.ramp_epochs);
   }

   return std::min(per_epoch, st->max_supply.amount - issued);
}

} // namespace eosio
