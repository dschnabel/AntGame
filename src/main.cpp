#include <functional>
#include <mutex>
#include <set>
#include <thread>
#include <map>

#include <websocketpp/config/asio_no_tls.hpp>
#include <websocketpp/server.hpp>

#include <boost/tokenizer.hpp>
#include <boost/thread/shared_mutex.hpp>
#include <boost/circular_buffer.hpp>

#include "BlockingQueue.hpp"

#define DEBUG 0
#define MAX_PLAYERS 12

typedef websocketpp::server<websocketpp::config::asio> server;

using websocketpp::connection_hdl;
using websocketpp::lib::placeholders::_1;
using websocketpp::lib::placeholders::_2;

using namespace std;

typedef struct exchange_data {
    uint8_t player; // 1 byte
    ushort x; // 2 bytes
    ushort y; // 2 bytes
    uint8_t key; // 1 byte
} __attribute__((packed, aligned(1))) exchange_data_t;

typedef struct broadcast {
    uint8_t data[sizeof(exchange_data_t) * MAX_PLAYERS];
    int count;
} broadcast_t;

typedef struct status {
    uint64_t index;
    ushort x;
    ushort y;
    uint8_t key;
} status_t;

enum Position {
    L_QUEEN,
    L_1,
    L_2,
    R_QUEEN,
    R_1,
    R_2
};

typedef struct player {
    Position position;
} player_t;

typedef struct input_message {
    connection_hdl hdl;
    uint64_t index;
    string str;
} input_message_t;

/**
 * need this operator since player_t is used as a key in a map
 */
bool operator < (const player_t &l, const player_t &r) { return l.position < r.position; }

class AntServer {
public:
    //******************* message monitoring thread ******************
    AntServer() {
        m_server.init_asio();

        m_server.set_socket_init_handler(bind(&AntServer::on_socket_init,this,_1,_2));
        m_server.set_open_handler(bind(&AntServer::on_open,this,_1));
        m_server.set_close_handler(bind(&AntServer::on_close,this,_1));
        m_server.set_message_handler(bind(&AntServer::on_message,this,_1,_2));
        m_server.set_reuse_addr(true);

        m_server.set_access_channels(websocketpp::log::alevel::none);

        message_index = 0;
        broadcast = (broadcast_t){0, 0};
    }

    ~AntServer() {
        m_server.stop_listening();
        m_server.stop();
    }

    void on_socket_init(websocketpp::connection_hdl, boost::asio::ip::tcp::socket & s) {
        boost::asio::ip::tcp::no_delay option(true);
        s.set_option(option);
    }

    void on_open(connection_hdl hdl) {
        boost::upgrade_lock<boost::shared_mutex> lock(connection_mutex);
        boost::upgrade_to_unique_lock<boost::shared_mutex> uniqueLock(lock);

        if (m_connections.empty()) {
            players[hdl] = (player_t){Position::L_QUEEN};
        } else {
            players[hdl] = (player_t){Position::R_QUEEN};
        }
        m_connections.insert(hdl);
    }

    void on_close(connection_hdl hdl) {
        boost::upgrade_lock<boost::shared_mutex> lock(connection_mutex);
        boost::upgrade_to_unique_lock<boost::shared_mutex> uniqueLock(lock);

        m_connections.erase(hdl);
    }

    void on_message(connection_hdl hdl, server::message_ptr msg) {
        input_queue.push((input_message_t){hdl, message_index++, msg->get_payload()});
    }

    void run(uint16_t port) {
        m_server.listen(port);
        m_server.start_accept();
        m_server.run();
    }
    //****************** /message monitoring thread ******************

    //************************* worker thread ************************
    void worker() {
        while (1) {
            input_message_t message = input_queue.pop();

            if (message.str.length() != sizeof(exchange_data_t)) {
                // invalid data, dismiss
                continue;
            }

            exchange_data_t buf;
            memcpy(&buf, message.str.c_str(), sizeof(exchange_data_t));
            if (buf.player != 187 // this is a magic number
                    || buf.x < 0 || buf.x > 1000
                    || buf.y < 0 || buf.y > 1000
                    || (buf.key != 'N' && buf.key != 'L' && buf.key != 'R')) {
                // invalid data, dismiss
                continue;
            }

            // data is found valid at this point

            boost::shared_lock<boost::shared_mutex> c_lock(connection_mutex);
            player_t p = players[message.hdl];
            c_lock.unlock();

            boost::upgrade_lock<boost::shared_mutex> d_lock(data_mutex); // shared, multiple readers
            boost::circular_buffer<status_t> &playerBuffer = data[p];
            // TODO validate input here
            d_lock.unlock();

            if (playerBuffer.size() == 0 || message.index > playerBuffer.back().index) {
                status_t s = {message.index, buf.x, buf.y, buf.key};

                boost::upgrade_to_unique_lock<boost::shared_mutex> uniqueLock(d_lock); // exclusive, one writer
                playerBuffer.set_capacity(10); // with 30 FPS this stores data for 300 ms
                playerBuffer.push_back(s);
                d_lock.release();
            } else {
                // a more recent data point already in buffer, dismiss
                continue;
            }

            boost::shared_lock<boost::shared_mutex> d_lock_2(data_mutex); // shared, multiple readers
            broadcast_t broadcast_copy = {0, 0};
            for(data_iterator iterator = data.begin(); iterator != data.end(); iterator++) {
                if (broadcast_copy.count >= MAX_PLAYERS) break;

                player_t p = iterator->first;
                status_t latest = (iterator->second).back();

                exchange_data_t broadcast_data = {p.position, latest.x, latest.y, latest.key};
                memcpy(broadcast_copy.data + (sizeof(exchange_data_t) * broadcast_copy.count),
                        &broadcast_data, sizeof(exchange_data_t));

                broadcast_copy.count++;
            }
            d_lock_2.unlock();

            lock_guard<mutex> lock(broadcast_mutex);
            memcpy(&broadcast, &broadcast_copy, sizeof(broadcast_t));
        }
    }
    //************************ /worker thread ************************

    //*********************** dispatcher thread **********************
    void dispatcher() {
        broadcast_t broadcast_copy = {0, 0};

        while (1) {
            usleep(50000);

            unique_lock<mutex> bc_lock(broadcast_mutex);
            memcpy(&broadcast_copy, &broadcast, sizeof(broadcast_t));
            bc_lock.unlock();
//            if (!bc_string.empty()) {
//                cout << bc_string << endl;
//            }

            boost::shared_lock<boost::shared_mutex> c_lock(connection_mutex);
            for (auto it : m_connections) {
                m_server.send(it, broadcast_copy.data, sizeof(exchange_data) * broadcast_copy.count,
                        websocketpp::frame::opcode::binary);
            }
        }
    }
    //********************** /dispatcher thread **********************
private:
    typedef set<connection_hdl,owner_less<connection_hdl>> con_list;
    typedef map <player_t, boost::circular_buffer<status_t>>::iterator data_iterator;
//    typedef tuple<connection_hdl, uint64_t, string> input_message;

    server m_server;
    con_list m_connections;
    boost::shared_mutex connection_mutex, data_mutex;
    mutex broadcast_mutex;

    uint64_t message_index;
    BlockingQueue<input_message_t> input_queue;
    map <connection_hdl, player_t, owner_less<connection_hdl>> players;
    map <player_t, boost::circular_buffer<status_t>> data;

    broadcast_t broadcast;
};

// Notes:
// - add -flto to compiler and linker command for optimization (don't use for debug build)
// - http://www.cloudping.info/
// - struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
// - top -p $(pgrep AntGame) -H
int main() {
    AntServer server;

#if DEBUG
    thread worker(bind(&AntServer::worker,&server));
#else
    int concurentThreadsSupported = std::thread::hardware_concurrency();
    int workerThreads = max(1, concurentThreadsSupported - 2);

    for (int i = 0; i < workerThreads; i++) {
        thread worker(bind(&AntServer::worker,&server)); // worker threads
        worker.detach();
    }
#endif

    thread dispatcher(bind(&AntServer::dispatcher,&server)); // dispatcher thread

    server.run(9002); // message monitoring thread
}
