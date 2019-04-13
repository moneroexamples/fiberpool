#include "../include/FiberPool.hpp"

//#define CATCH_CONFIG_MAIN
#define CATCH_CONFIG_RUNNER

#include <boost/fiber/detail/thread_barrier.hpp>

#include "catch.hpp"

using namespace FiberPool;
using namespace DefaultFiberPool;
using namespace std::literals::chrono_literals;

using boost::this_fiber::sleep_for;
using boost::fibers::future;
using boost::fibers::detail::thread_barrier;
using boost::fibers::channel_op_status;

TEST_CASE("No of threads and fibers", "[default-pool]") 
{
    auto no_of_threads = no_of_defualt_threads();

	SECTION("Initial")
	{
    	CHECK(get_pool().threads_no() == no_of_threads);
	    CHECK(get_pool().fibers_no() == 0);
	}

	SECTION("Submit 5 task")
	{
		// testing for number of currently running fibers
		// can be tricky, because all fibers are executed
		// asynchroniusly on a different thread than 
		// this code is run. 

		// we can solve this problem by using conditional variables
		// and suspending the current thread till number
		// of fibers reaches given value
		
		std::atomic<size_t> fiber_count {0};
		boost::fibers::mutex mtx_count {};
		boost::fibers::condition_variable_any cnd_count {};
		
		std::vector<std::optional<future<void>>> ofs;

		for (size_t i = 0; i < 5; ++i)
		{
			ofs.emplace_back(submit_job(
			[&fiber_count, &cnd_count]()
			{
				// increase fiber cound and notify one thread
				// (i.e., the thread of this test) of the update
				++fiber_count;
				cnd_count.notify_one();
				
				sleep_for(1s);
			}));
		}
		
		{
			// the main thread waits here till fiber_count reaches 5
			std::unique_lock lk {mtx_count};
			cnd_count.wait(lk, [&fiber_count](){return fiber_count == 5;});

			// when this happens we execute a check whether fibers_no()
			// actually indicated 5 fibers
	    	CHECK(get_pool().fibers_no() == 5);
		}
		
        // wait for all fibers to finish
		for (auto&& of: ofs)
			of->wait();
	    
        // now fibers number should be 0
        CHECK(get_pool().fibers_no() == 0);
	}
}

TEST_CASE("Return value", "[default-pool]") 
{
	SECTION("Using return statement, no input params")
	{
		auto opt_future = submit_job([]()
		{
			size_t r {0};
			for (auto i: {1,2,3})
				r += i;

			return r;
		});
    
        REQUIRE(bool {opt_future} == true);

        auto r = opt_future->get();

        CHECK(r == 6);    
	}

	SECTION("Using return statement, with input params")
	{
        struct InputObj
        {
            size_t val {5};
        } in_obj;

		auto opt_future = submit_job(
        [](auto const& _in_obj)
		{
			size_t r {0};
			for (auto i: {1,2,3})
				r += _in_obj.val;

			return r;
		}, std::cref(in_obj));

        REQUIRE(bool {opt_future} == true);

        auto r = opt_future->get();

        CHECK(r == 15);    
	}
	
    SECTION("Using reference param")
	{
        std::vector<size_t> vec;

		auto opt_future = submit_job(
        [](auto& _in_obj)
		{
            sleep_for(100ms);
            _in_obj = {1,2,3};
		}, std::ref(vec));
        
        REQUIRE(bool {opt_future} == true);

        opt_future->wait();

        size_t vec_sum {};

        for (auto v: vec)
            vec_sum += v;

        CHECK(vec_sum == 6);    
	}
}

TEST_CASE("Throw exception", "[default-pool]") 
{
    auto opt_future1 = submit_job([]()
    {
        sleep_for(1s);
        throw std::runtime_error("some exception");
        return false;
    });
    
    auto opt_future2 = submit_job([]()
    {
        sleep_for(500ms);
        throw std::runtime_error("some exception");
        return false;
    });

    CHECK_THROWS(opt_future1->get());
    CHECK_THROWS(opt_future2->get());

    CHECK(get_pool().fibers_no() == 0);
}

/**
 * We are going to mock behaviour
 * of boost::fiber::buffered_channel to
 * test what's going to happen when the push and pop
 * of a task from the channel fails.
 */
template<typename T>
class MockChannel
{
    public:

    using value_type = T;

    explicit MockChannel(...) {}
   
    channel_op_status push(...)
    {
        return channel_op_status::full;
    }
    
    template <typename U> 
    channel_op_status pop(U&& value)
    {
        return channel_op_status::full;
    }
    
    void close() noexcept 
    {
        ;// do nothing
    }
};

TEST_CASE("pushing task fails", "[non-default-pool]")
{
    ::FiberPool::FiberPool<MockChannel> fiber_pool {1};

    auto opt_future = fiber_pool.submit([](){;});

    CHECK(bool {opt_future} == false);
}



int main( int argc, char* argv[] ) 
{
  int result = Catch::Session().run( argc, argv );
  close(); // close the default pool
  return result;
}
