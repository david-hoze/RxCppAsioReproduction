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

#if 1
    // minimum changes to work
    //

    static rxcpp::subjects::subject<int> m_picture_processed, m_finished, m_picture;

    auto start = std::chrono::system_clock::now();
    IoServiceContext::GetContext();
    std::this_thread::sleep_for(std::chrono::seconds (1));
    auto asio_coordination = rxcpp::synchronize_in_asio(IoServiceContext::GetContext()->m_ios);

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
                        // take() completed this subscription to this subject, 
                        // now complete all the other subscriptions to all the subjects
                        m_picture_processed.get_subscriber().on_completed();
                        m_picture.get_subscriber().on_completed();
                        m_finished.get_subscriber().on_completed();
                    });

    m_picture.get_observable()
            .flat_map([=](int i) {
                return rxcpp::observable<>::defer(
                        [=]() {
//                            std::this_thread::sleep_for(std::chrono::milliseconds (10));
                            std::cout << "Got event\n";
                            m_picture_processed.get_subscriber().on_next(1);
                            return rxcpp::observable<>::just<int>(1);
                        })
                        .subscribe_on(asio_coordination);
            }, asio_coordination) // override default - each subscribe_on above is 
                                  // interleaved across threads and this merges them 
                                  // back onto one sequence.
            .subscribe();

    for (int i=0; i < TEST_COUNT; i++) {
        std::cout << "Sending event\n";
        m_picture.get_subscriber().on_next(1);
    }

    // wait for all the subjects to complete
    m_picture.get_observable()
            .merge(
                asio_coordination, // merge the different threads back into one sequence
                m_picture_processed.get_observable(), 
                m_finished.get_observable())
            .as_blocking()
            .subscribe();

#else
    // avoid subjects and prefer operators to subscribe
    //

    static rxcpp::subjects::subject<int> m_picture_processed, m_picture;

    auto start = std::chrono::system_clock::now();
    IoServiceContext::GetContext();
    std::this_thread::sleep_for(std::chrono::seconds (1));
    auto asio_coordination = rxcpp::synchronize_in_asio(IoServiceContext::GetContext()->m_ios);

    std::cout << "starting processor and listener\n";

    auto proccessor = m_picture_processed.get_observable()
        .scan(0, [](int sum, int i) { return sum+=i; })
        .tap([](int i) {
            std::cout << "Process event " << i << "\n";
        })
        .take(TEST_COUNT) // processor completes after all items are processed
        .finally([start]() {
            auto duration = int(0.5 + std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now() - start).count());
            std::cout << "Duration: " << duration << "\n";
        })
        .publish()
        .connect_forever();

    auto listener = m_picture.get_observable()
        .tap([](int i) {
            std::cout << "Sending event " << i + 1 << "\n";
        })
        .observe_on(asio_coordination)
        .tap([](int i) {
            std::cout << "Listen event "<< i + 1 <<"\n";
        })
        .finally([]{
            std::cout << "listener complete\n";
        })
        .map([=](int){ 
            return rxcpp::sources::defer([=]{
                return rxcpp::sources::just(1)
                    .subscribe_on(asio_coordination); 
            }).as_dynamic();
        })
        .merge(asio_coordination) // override default - each subscribe_on above is 
                                  // interleaved across threads and this merges them 
                                  // back onto one sequence.
        .tap(m_picture_processed.get_subscriber()) // forward to the processor
        .publish()
        .connect_forever();

    std::cout << "Sending Events\n";
    for (int i=0; i < TEST_COUNT; i++) {
        m_picture.get_subscriber().on_next(i);
    }

    std::cout << "Waiting for processor\n";
    proccessor
            .as_blocking()
            .count();

    std::cout << "close subjects\n";
    m_picture.get_subscriber().on_completed();

    std::cout << "Waiting for listener\n";
    listener
            .as_blocking()
            .count();

#endif

    return 0;
}
