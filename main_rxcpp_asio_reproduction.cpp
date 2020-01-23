#include "rxcpp/rx.hpp"
#include "rx-asio.h"

#define TEST_COUNT 500

boost::asio::io_service ios;
std::vector<std::thread> threads;

struct IoServiceContext
{
    static auto GetContext()
    {
        static auto TheContext = std::make_shared<IoServiceContext>();
        return TheContext;
    };
    boost::asio::io_service m_ios;
    std::vector<std::thread> m_threads;
    boost::asio::io_service::work m_work;

    IoServiceContext() : m_work(m_ios) {
        unsigned int thread_pool_size = std::thread::hardware_concurrency() * 2;
        if (thread_pool_size == 0)
            thread_pool_size = 2;

        for (unsigned int i = 0; i < thread_pool_size; i++)
        {
            auto th = std::thread([this]() {
                try {
                    m_ios.run();
                }
                catch (const std::exception& e)
                {
                    auto w = e.what();
                }
                catch (...)
                {
                }
            });
            th.detach();
            m_threads.push_back(std::move(th));
        }

    };
    //~IoServiceContext(); // default dtor() good enough

};

int main(int argc, const char *const argv[]) {

    static rxcpp::subjects::subject<int> m_picture_processed, m_finished, m_picture;

    auto start = std::chrono::system_clock::now();
    IoServiceContext::GetContext();
    std::this_thread::sleep_for(std::chrono::seconds (1));

    m_picture_processed.get_observable()
            .take(TEST_COUNT)
            .scan(0, [](int sum, int i) { return sum+=i; })
            .subscribe(
                    [](int i) {
                        std::cout << "Got event " << i << "\n";
                    },
                    [start]() {
                        auto duration = int(0.5 + std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now() - start).count());
                        std::cout << "Duration: " << duration << "\n";
                        m_finished.get_subscriber().on_completed();
                    });

    m_picture.get_observable()
            .flat_map([](int i) {
                return rxcpp::observable<>::defer(
                        []() {
//                            std::this_thread::sleep_for(std::chrono::milliseconds (10));
                            std::cout << "Got event\n";
                            m_picture_processed.get_subscriber().on_next(1);
                            return rxcpp::observable<>::just<int>(1);
                        })
                        .subscribe_on(rxcpp::synchronize_in_asio(IoServiceContext::GetContext()->m_ios));
            })
            .subscribe();

    for (int i=0; i < TEST_COUNT; i++) {
        std::cout << "Sending event\n";
        m_picture.get_subscriber().on_next(1);
    }

    m_finished.get_observable()
            .as_blocking()
            .subscribe();
    return 0;
}
