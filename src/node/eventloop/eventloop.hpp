#pragma once
#include "address_manager/address_manager.hpp"
#include "api/callbacks.hpp"
#include "block/chain/offender.hpp"
#include "block/chain/signed_snapshot.hpp"
#include "chain_cache.hpp"
#include "chainserver/state/update/update.hpp"
#include "communication/stage_operation/result.hpp"
#include "eventloop/sync/block_download/block_download.hpp"
#include "eventloop/sync/header_download/header_download.hpp"
#include "eventloop/sync/request_sender_declaration.hpp"
#include "eventloop/timer.hpp"
#include "mempool/mempool.hpp"
#include "mempool/subscription_declaration.hpp"
#include "peerserver/peerserver.hpp"
#include "sync/sync_state.hpp"
#include "types/chainstate.hpp"
#include "types/conndata.hpp"
#include <condition_variable>
#include <mutex>
#include <queue>
#include <thread>

class Connection;
class Rcvbuffer;
class Reader;
class Eventprocessor;
class EndAttorney;
struct Config;

struct ForkMsg;
struct AppendMsg;
struct Inspector;

class ChainServer;
namespace BlockDownload {
class Attorney;
}

class Eventloop {
    using duration = std::chrono::steady_clock::duration;
    using seconds = std::chrono::seconds;
    using StateUpdate = chainserver::state_update::StateUpdate;
    friend class BlockDownload::Attorney;
    friend class EndAttorney;

public:
    friend struct Inspector;
    Eventloop(PeerServer&, ChainServer& ss, const Config& config);
    ~Eventloop();

    // API callbacks
    using SignedSnapshotCb = std::function<void(const tl::expected<SignedSnapshot, int32_t>&)>;
    using InspectorCb = std::function<void(const Eventloop&)>;

    /////////////////////
    // Async functions
    // called by other threads

    void async_state_update(StateUpdate&& s);
    void async_mempool_update(mempool::Log&& s);
    bool async_process(std::shared_ptr<Connection> c);
    void async_erase(std::shared_ptr<Connection> c, int32_t error);
    void async_shutdown(int32_t reason);
    void async_report_failed_outbound(EndpointAddress);
    void async_stage_action(stage_operation::Result);

    void api_get_peers(PeersCb&& cb, bool filterThrottled);
    void api_get_synced(SyncedCb&& cb);
    void api_get_hashrate(HashrateCb&& cb, size_t n = 100);
    void api_get_hashrate_chart(HashrateChartCb&& cb);
    void api_get_hashrate_chart(NonzeroHeight from, NonzeroHeight to, size_t window, HashrateChartCb&& cb);
    void api_inspect(InspectorCb&&);

    void start_async_loop();

private:
    std::vector<EndpointAddress> get_db_peers(size_t num);
    //////////////////////////////
    // Important event loop functions
    void loop();
    bool has_work();
    void work();
    bool check_shutdown();
    void process_connection(std::shared_ptr<Connection> c);

    //////////////////////////////
    // Private async functions

    void async_forward_blockrep(uint64_t conId, std::vector<BodyContainer>&& blocks);

    //////////////////////////////
    // Connection related functions
    void erase(Conref cr, int32_t error);
    [[nodiscard]] bool insert(Conref cr, const InitMsg& data); // returns true if requests might be possbile
    void close(Conref cr, Error reason);
    void close_by_id(uint64_t connectionId, int32_t reason);
    void close(const ChainOffender&);
    void close(Conref cr, ChainError);
    void report(const ChainOffender&) { };

    ////////////////////////
    // Handling incoming messages
    void dispatch_message(Conref cr, Rcvbuffer& rb);
    void handle_msg(Conref cr, PingMsg&&);
    void handle_msg(Conref cr, PongMsg&&);
    void handle_msg(Conref cr, BatchreqMsg&&);
    void handle_msg(Conref cr, BatchrepMsg&&);
    void handle_msg(Conref cr, ProbereqMsg&&);
    void handle_msg(Conref cr, ProberepMsg&&);
    void handle_msg(Conref cr, BlockreqMsg&&);
    void handle_msg(Conref cr, BlockrepMsg&&);
    void handle_msg(Conref cr, InitMsg&&);
    void handle_msg(Conref cr, AppendMsg&&);
    void handle_msg(Conref cr, SignedPinRollbackMsg&&);
    void handle_msg(Conref cr, ForkMsg&&);
    void handle_msg(Conref cr, TxnotifyMsg&&);
    void handle_msg(Conref cr, TxreqMsg&&);
    void handle_msg(Conref cr, TxrepMsg&&);
    void handle_msg(Conref cr, LeaderMsg&&);

    ////////////////////////
    // convenience functions
    void consider_send_snapshot(Conref);

    ////////////////////////
    // assign work to connections
    void do_requests();
    void send_requests(Conref cr, const std::vector<Request>&);

    ////////////////////////
    // send functions
    template <typename T>
    void send_request(Conref cr, const T& req);

    friend class RequestSender;
    RequestSender sender() { return RequestSender(*this); };
    void send_init(Conref cr);

    ////////////////////////
    // Handling timeout events
    void handle_expired(Timer::Event&& data);
    void expired_pingsleep(Conref cr);
    void expired_init(Conref cr);

    ////////////////////////
    // Timer functions
    void cancel_timer(Timer::iterator& ref);
    void send_ping_await_pong(Conref cr);
    void received_pong_sleep_ping(Conref cr);
    void update_wakeup();

    ////////////////////////
    // Timeout callbacks
    template <typename T>
    requires std::derived_from<T, Timer::WithConnecitonId>
    void handle_timeout(T&&);
    void handle_timeout(Timer::Connect&&);
    void handle_connection_timeout(Conref, Timer::SendPing&&);
    void handle_connection_timeout(Conref, Timer::ThrottledSend&&);
    void handle_connection_timeout(Conref, Timer::Expire&&);
    void handle_connection_timeout(Conref, Timer::CloseNoReply&&);
    void handle_connection_timeout(Conref, Timer::CloseNoPong&&);
    void on_request_expired(Conref cr, const Proberequest&);
    void on_request_expired(Conref cr, const Batchrequest&);
    void on_request_expired(Conref cr, const Blockrequest&);

    ////////////////////////
    // blockdownload result
    void process_blockdownload_stage();

    ////////////////////////
    // establish new connections
    void connect_scheduled();

    ////////////////////////
    // event types
    struct OnRelease {
        std::shared_ptr<Connection> c;
        int32_t error;
    };
    struct OnProcessConnection {
        std::shared_ptr<Connection> c;
    };
    struct OnForwardBlockrep {
        uint64_t conId;
        std::vector<BodyContainer> blocks;
    };
    struct OnFailedAddressEvent {
        EndpointAddress a;
    };
    struct OnPinAddress {
        EndpointAddress a;
    };
    struct OnUnpinAddress {
        EndpointAddress a;
    };
    struct GetHashrateChart {
        HashrateChartCb cb;
        NonzeroHeight from;
        NonzeroHeight to;
        size_t window;
    };
    struct GetHashrate {
        HashrateCb cb;
        size_t n;
    };
    struct GetPeers{
        PeersCb callback;
        bool filterThrottled;
    };
    // event queue
    using Event = std::variant<OnRelease, OnProcessConnection,
        StateUpdate, SignedSnapshotCb, GetPeers, SyncedCb, stage_operation::Result,
        OnForwardBlockrep, OnFailedAddressEvent, InspectorCb, GetHashrate, GetHashrateChart,
        OnPinAddress, OnUnpinAddress, mempool::Log>;

public:
    bool defer(Event e);

private:
    // event handlers
    void handle_event(OnRelease&&);
    void handle_event(OnProcessConnection&&);
    void handle_event(StateUpdate&&);
    void handle_event(GetPeers&&);
    void handle_event(SyncedCb&&);
    void handle_event(SignedSnapshotCb&&);
    void handle_event(stage_operation::Result&&);
    void handle_event(OnForwardBlockrep&&);
    void handle_event(OnFailedAddressEvent&&);
    void handle_event(InspectorCb&&);
    void handle_event(GetHashrate&&);
    void handle_event(GetHashrateChart&&);
    void handle_event(OnPinAddress&&);
    void handle_event(OnUnpinAddress&&);
    void handle_event(mempool::Log&&);

    // scheduling 
    void send_throttled(Conref cr, Sndbuffer, duration d);
    
    // chain updates
    using Append = chainserver::state_update::Append;
    using Fork = chainserver::state_update::Fork;
    using RollbackData = chainserver::state_update::RollbackData;
    void update_chain(Append&&);
    void update_chain(Fork&&);
    void update_chain(RollbackData&&);
    void coordinate_sync();

    void initialize_block_download();
    ForkHeight set_stage_headers(Headerchain&&);

    // log
    void log_chain_length();

    // checkers
    void verify_rollback(Conref, const SignedPinRollbackMsg&); // throws

    ////////////////////////
    // convenience functions
    const ConsensusSlave& consensus() { return chains.consensus_state(); }

    ////////////////////////
    // register sync state

    void update_sync_state();

private: // private data
    //
    ChainServer& stateServer;
    // Conndatamap connections;
    StageAndConsensus chains;
    mempool::Mempool mempool; // copy of chainserver mempool

    address_manager::AddressManager connections;

    Timer timer;
    std::optional<Timer::iterator> wakeupTimer;

    // Request related
    size_t activeRequests = 0;
    size_t maxRequests = 10;

    //
    auto signed_snapshot() const { return chains.signed_snapshot(); };
    HeaderDownload::Downloader headerDownload;
    BlockDownload::Downloader blockDownload;
    mempool::SubscriptionMap mempoolSubscriptions;
    SyncState syncState;

    ////////////////////////////
    // mutex protected varibales
    ////////////////////////////
    std::condition_variable cv;
    std::mutex mutex;
    bool haswork = false;
    int32_t closeReason = 0;
    bool blockdownloadHalted = false;
    std::queue<Event> events;
    std::thread worker; // worker (constructed last)
};

template <typename T>
requires std::derived_from<T, IsRequest>
void RequestSender::send(Conref cr, const T& req)
{
    e.send_request(cr, req);
}
inline bool RequestSender::finished()
{
    return e.maxRequests <= e.activeRequests;
}
