#include "all.hpp"
#include "block/chain/history/history.hpp"
#include "chainserver/account_cache.hpp"
#include "general/address_funds.hpp"

namespace api {
void Block::set_reward(Reward r)
{
    if (_reward.has_value())
        throw std::runtime_error("Database error, each block can only have one reward transaction");
    _reward = r;
}

void Block::push_history(const Hash& txid,
    const std::vector<uint8_t>& data, chainserver::AccountCache& cache,
    PinFloor pinFloor)
{
    auto parsed = history::parse_throw(data);
    if (std::holds_alternative<history::TransferData>(parsed)) {
        auto& d = std::get<history::TransferData>(parsed);
        transfers.push_back(
            api::Block::Transfer {
                .fromAddress = cache[d.fromAccountId].address,
                .fee = d.compactFee.uncompact(),
                .nonceId = d.pinNonce.id,
                .pinHeight = d.pinNonce.pin_height(pinFloor),
                .txhash = txid,
                .toAddress = cache[d.toAccountId].address,
                .amount = d.amount });
    } else {
        auto& d = std::get<history::RewardData>(parsed);
        auto toAddress = cache[d.toAccountId].address;
        set_reward(Reward { txid, toAddress, d.miningReward });
    }
}
}
