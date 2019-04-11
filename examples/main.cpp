#include "../include/FiberPool.hpp"

#include <iostream>
#include <sstream>

using namespace std::literals::chrono_literals;

/**
 * Example of a functor that will
 * be executed by a FiberPool
 */
struct SomeFunctor
{
    std::string msg {};

    void 
    operator()(std::string const& msg)
    {
        size_t i = 0;

        auto tid = std::this_thread::get_id();
        
        while(++i < 5)
        {

    		auto sleep_time = std::rand() % 3 + 1;

			// since this functor will be run as fiber
			// we need to use fiber friendly calls
			// such as boost::this_fiber::yield()
			// or boost::this_fiber::slieep_for(time)
		    boost::this_fiber::sleep_for(
        	    std::chrono::seconds(sleep_time));

            auto tid_new = std::this_thread::get_id();
            
            std::cout << msg << std::endl;

            if (tid_new != tid)
            {
                // notify when the fiber executing this 
                // functor changes a worker thread
                std::ostringstream ss;

                ss <<  msg << " has new thread: " 
                    << tid_new << '\n';
                std::cout << ss.str() << std::flush;

                tid = tid_new;
            }
        }
    }
};


uint64_t
factorial(uint64_t n)
{
   if(n > 1)
   {
       std::ostringstream os;
       os << "Factorial n: " << n << '\n';
       std::cout << os.str() << std::flush;

	   // give other fiber chance to execute	
	   // not that we need to yeild() from time to time
	   // in our tasks so that other finers in the same 
	   // thread have chance to run		
       boost::this_fiber::yield();

       return n * factorial(n - 1);
   }

   return 1;
}


void 
find_primes(uint64_t n, std::vector<uint64_t>& primes)
{
    for(uint64_t num = 1; num <= n; ++num)
	{
        uint64_t count = 0;

        for(uint64_t i = 2; i <= num / 2; i++)
		{
            if (num % i == 0)
			{
                count++;
                break;
            }
        }

	    // give other fiber chance to execute	
		boost::this_fiber::yield();

        if (count == 0 && num != 1)
		{
			std::ostringstream os;
			os << "Find_primes num: " << num << '\n';
			std::cout << os.str() << std::flush;
			primes.push_back(num);
		}
    }
};


void
throws()
{
    std::ostringstream os;
    os << "Will throws exception soon\n";
    std::cout << os.str() << std::flush;

    boost::this_fiber::sleep_for(3s);

    // task will throw. but since we are running 
    // it as packaged_tasked in a fiber
    // the exception will be intercepted by the 
    // task's future. calling get() on the future
    // will rethrow it or we can get pointer to it.
    throw std::runtime_error("Exception thrown in " 
                             "a task running in fiber " 
                             "in the pool");
}

void
custom_pool()
{
	// use only one thread
	auto no_worker_threads {1u};

	// any short queue
	auto work_queue_size {4u};

	FiberPool::FiberPool fiber_pool {
					no_worker_threads,
					work_queue_size};

	// submit a function to the pool with one paramter
	auto factorial_future = fiber_pool.submit(&factorial, 20);

	// submit another function which will return result 
	// by reference
	std::vector<uint64_t> primes;

	auto primes_future = fiber_pool.submit(&find_primes, 
										   20, 
										   std::ref(primes));


	// wait for factorial task to finish
	auto factorial_calculated = factorial_future.get();

	std::cout << "Factorial calcualted: " 
			  << factorial_calculated << std::endl;

	// primes_future does not return any value because
	// it uses reference of an input parameter to provide
	// found prime values.
	primes_future.get();

	std::cout << "Primes found: ";

	for (auto& p: primes)
		std::cout << p << ", ";

	std::cout << "\b\b " <<  std::endl;

	fiber_pool.close_queue();
}




int
main(int argc, const char *argv[])
{
// In the few following examples we are going 
// to use DefaultFiberPool only


// submit lambda to the pool
auto future_1 = DefaultFiberPool::submit_job(
        [](){
                size_t i = 0;

                while(++i < 5)
                {
					std::ostringstream os;
                    os  << "lambda task i: " << i <<  '\n';
					std::cout << os.str() << std::flush;
				    boost::this_fiber::sleep_for(2s);
                }

                return i;
            });

// we are not waiting here for it to finish running
// as the fiber created for the lumbda submitted is 
// executed by a thread created in the DefaultFiberPool. 
// later we are going to use
// future_1 to wait for the result when we want it.
//
// instead we just proceed to submit next lambda

// submit 10 functors to the pool
using task_future_t = boost::fibers::future<void>;
std::vector<task_future_t> futures; 

for (auto i = 0; i < 10; i++)
{

	 // the functor will notify when a fiber executing it
	 // has switch thread
	 auto&& a_future = DefaultFiberPool::submit_job(SomeFunctor(), 
								 "functor: " + std::to_string(i));

	 // we need to store the futures, otherwise
	 // they will block, bacause we use our FiberPool::TaskFuture
	 // wraper over orginal boost::fiber::future
	 futures.push_back(std::forward<task_future_t>(a_future));
}

// again, not waiting for any of these functors to finish. they
// are run by fibers in the FiberPool. We just proceed to submition
// of another lambda


// submit another lambda which will take parameter
// by reference
std::string val {};
std::string msg {"FiberPool"};

auto future_2 = DefaultFiberPool::submit_job(
        [](auto const& in_str,  auto& out_str)
        {
			// suspend this task's fiber for 3s
			boost::this_fiber::sleep_for(3s);

			std::ostringstream os;
			os << "setting val to:" << in_str << '\n';
			std::cout << os.str() << std::flush;

            out_str = in_str;

        }, std::cref(msg), std::ref(val));


auto future_3 = DefaultFiberPool::submit_job(throws);

// now we dont have any more tasks to submit, so we can 
// wait for their completion.
// we use future to wait.

auto result_1 = future_1.get();

std::cout << "val: " << val << std::endl;
std::cout << "result from first lambda: " << result_1 << std::endl;


// wait for future 3
future_3.wait();

// get exception pointer to check if the task thrown something
std::exception_ptr exp_ptr {future_3.get_exception_ptr()};

if (exp_ptr)
{
    // if exception was thrown, rethrow it
    std::cout << "have some exception" << std::endl;
    try {
        std::rethrow_exception(exp_ptr);
    }
    catch(std::exception const& e)
    {
        std::cout << "catched rethrown exception: " 
                  << e.what() << std::endl;
    }
}


DefaultFiberPool::close();

// now we try using custom pool
custom_pool();


return EXIT_SUCCESS;
}
