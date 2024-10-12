#pragma once
#include <variant>
namespace api {
struct AccountHistory;
struct AccountIdOrAddress;;
struct AddressCount;
struct AddressWithId;
struct Balance;
struct Block;
struct ChainHead;
struct HashrateBlockChart;
struct HashrateChartRequest;
struct HashrateInfo;
struct HashrateTimeChart;
struct Head;
struct HeightOrHash;
struct MempoolEntries;
struct MiningState;
struct Peerinfo;
struct PeerinfoConnections;
struct Raw;
struct RewardTransaction;
struct Richlist;
struct Rollback;
struct Round16Bit;
struct TransactionsByBlocks;
struct TransferTransaction;
struct Wallet;
using Transaction = std::variant<RewardTransaction, TransferTransaction>;
}
