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
#include "Status.hpp"

#define DEBUG 0

typedef websocketpp::server<websocketpp::config::asio> server;

using websocketpp::connection_hdl;
using websocketpp::lib::placeholders::_1;
using websocketpp::lib::placeholders::_2;

using namespace std;

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

        message_count = 0;
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
            players[hdl] = "L1";
        } else {
            players[hdl] = "R1";
        }
        m_connections.insert(hdl);
    }

    void on_close(connection_hdl hdl) {
        boost::upgrade_lock<boost::shared_mutex> lock(connection_mutex);
        boost::upgrade_to_unique_lock<boost::shared_mutex> uniqueLock(lock);

        m_connections.erase(hdl);
    }

    void on_message(connection_hdl hdl, server::message_ptr msg) {
        input_queue.push(input_message(hdl, message_count++, msg->get_payload()));
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
            input_message m = input_queue.pop();

            string message = get<2>(m);
            // get values
            int x, y;
            char key;
            boost::tokenizer<> tok(message);
            boost::tokenizer<>::iterator beg = tok.begin();
            if (distance(tok.begin(), tok.end()) == 3) {
                string::size_type sz;
                x = stoi(*(beg++), &sz);
                y = stoi(*(beg++), &sz);
                key = (*(beg++)).at(0);
            } else {
                // invalid message, dismiss
                continue;
            }

            connection_hdl hdl = get<0>(m);
            uint64_t message_index = get<1>(m);

            boost::shared_lock<boost::shared_mutex> c_lock(connection_mutex);
            string playerName = players[hdl];
            c_lock.unlock();

            boost::upgrade_lock<boost::shared_mutex> d_lock(data_mutex); // shared, multiple readers
            boost::circular_buffer<Status> &playerBuffer = data[playerName];
            // TODO validate input here
            d_lock.unlock();

            if (playerBuffer.size() == 0 || message_index > playerBuffer.back().getIndex()) {
                Status status(message_index, x, y, key);

                boost::upgrade_to_unique_lock<boost::shared_mutex> uniqueLock(d_lock); // exclusive, one writer
                playerBuffer.set_capacity(10); // with 30 FPS this stores data for 300 ms
                playerBuffer.push_back(status);
                d_lock.release();
            } else {
                // a more recent data point already in buffer, dismiss
                continue;
            }

            boost::shared_lock<boost::shared_mutex> d_lock_2(data_mutex); // shared, multiple readers
            stringstream ss;
            for(data_iterator iterator = data.begin(); iterator != data.end(); iterator++) {
                string player = iterator->first;
                Status latest = (iterator->second).back();

                ss << player << ":" << latest << "#";
            }
            d_lock_2.unlock();

            lock_guard<mutex> lock(broadcast_mutex);
            broadcast = ss.str();
        }
    }
    //************************ /worker thread ************************

    //*********************** dispatcher thread **********************
    void dispatcher() {
        while (1) {
            usleep(50000);

            unique_lock<mutex> bc_lock(broadcast_mutex);
            string bc_string = broadcast;
            bc_lock.unlock();
//            if (!bc_string.empty()) {
//                cout << bc_string << endl;
//            }

            boost::shared_lock<boost::shared_mutex> c_lock(connection_mutex);
            for (auto it : m_connections) {
                m_server.send(it, bc_string, websocketpp::frame::opcode::text);
            }
        }
    }
    //********************** /dispatcher thread **********************
private:
    typedef set<connection_hdl,owner_less<connection_hdl>> con_list;
    typedef map <string, boost::circular_buffer<Status>>::iterator data_iterator;
    typedef tuple<connection_hdl, uint64_t, string> input_message;

    server m_server;
    con_list m_connections;
    boost::shared_mutex connection_mutex, data_mutex;
    mutex broadcast_mutex;

    uint64_t message_count;
    BlockingQueue<input_message> input_queue;
    map <connection_hdl, string, owner_less<connection_hdl>> players;
    map <string, boost::circular_buffer<Status>> data;

    string broadcast;
};

// Notes:
// - add -flto to compiler and linker command for optimization (don't use for debug build)
// - http://www.cloudping.info/
// - struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
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
