## Header only boost::fiber thread pool library

A single-file header-only C++17 library providing 
a [boost::fiber](https://github.com/boostorg/fiber) thread pool.

The library is based on 
[A Platform-Independent Thread Pool Using C++14](http://roar11.com/2016/01/a-platform-independent-thread-pool-using-c14/)
and most description provided there also applies to FiberPool. 

The main difference is that 
FiberPool does not use its own custom work queue, as in the upstream code. Instead
it uses a thread-safe [work queue](https://www.boost.org/doc/libs/1_69_0/libs/fiber/doc/html/fiber/synchronization/channels/buffered_channel.html)
provided by fiber library. Also we use `boost::fibers::future` directly, rather than
wrapping as in the upstream code.

## Motivation

In my projects I usually need to run many threads. There are two main issues
with that: 
 - context switching between
bettween threads can be expesive, and
 - working with multiple threads requires
careful synchronization of access to common resources. 


The boost::fiber library minimizes theses issues as it allows 
to run multiple fibers on single thread or multiple threads, concurently. 
Therefore one can run multiple  "simultaneously" tasks/fibers on a 
single thread without worring about synchronization "simultaneously".


However, the boost::fiber library lacks a tool for easy submitions and
managment of tasks/fibers to be executed by several threads. The FiberPool
library addresses this, by providing an easy way for spanning server 
worker threads and submitting tasks/fibers to them.


## Requirements

 - C++17 compiler
 - boost::fiber 

## Example compilation 

To compile the example provided:

```bash
git clone git@github.com:moneroexamples/fiberpool.git

cd fiberpool && mkdir build && cd build

cmake ..

make 
```
		
## Example usage

More examples are in the `examples` folder.

A key thing to note is that all tasks submitted to the FiberPool
**must be fiber friendly**. This means that we have to use 
`boost::this_fiber::yeid()` in
tasks that use long running loops 
to give other fibers a chance to run. Otherwise such tasks will block
its worker thread, and subsequently, all fibers running in the thread. 


Similarly, we use `boost::this_fiber::sleep_for()` instead
of `std::this_thread::sleep_for()` to put to sleep one fiber, rather than
entire thread with all its fibers. The same goes for `boost::fibers::mutex` 
and `boost::fibers::condition_variable`, which
fiber library also provides to be used instead of those in `std`.

#### Lambda task

```C++
// submit lambda fiber task to the pool
auto future_1 = DefaultFiberPool::submit_job(
        [](){
                size_t i = 0;

                while(++i < 5)
                {
                    std::cout << "lambda task" << std::endl;
				    boost::this_fiber::sleep_for(5s);
                }

                return i;
            });

//
// do other things here if needed, e.g., submit second fiber task
//

// wait for the result
auto result = future_1.get();
```

#### Lambda task with parameters

```C++
std::string val {}; // will hold result of the task

std::string msg {"FiberPool"}; // input variabile for the task

auto future_2 = DefaultFiberPool::submit_job(
        [](auto const& in_str, auto& out_str)
        {
    		// give other fibers a chance to run
			boost::this_fiber::yield();

			// when we get to be executed again, resume
			// from here
            out_str = in_str;

        }, std::cref(msg), std::ref(val));
//
// do other things here if needed, e.g., submit second fiber task
//

// wait for the fiber task to finish.
future_2.get();

// once task finishes, val is ready 
std::cout << val << std::endl;
```

#### Functor

```C++
struct FunctorTask
{
	size_t counter {10};

	void operator()()
	{
		while(counter --> 0)
		{
			// give other fibers a chance to run
			boost::this_fiber::yield();
			std::cout << "Counter: " << counter << std::endl;
		}
	}
}

FunctorTask task {.counter = 20};

auto a_future = DefaultFiberPool::submit_job(task);

//
// do other things here if needed, e.g., submit second fiber task
//

a_future.get();
```

#### Function


```C++
uint64_t factorial(uint64_t n)
{
   if(n > 1)
   {
	   // give other fiber chance to execute	
	   // not that we need to yeild() from time to time
	   // in our tasks so that other finers in the same 
	   // thread have chance to run		
       boost::this_fiber::yield();

       return n * factorial(n - 1);
   }

   return 1;
}

auto factorial_future 
	= DefaultFiberPool::submit_job(&factorial, 50);

//
// do other things here if needed, e.g., submit second fiber task
//

auto factorial_calculated = factorial_future.get();

std::cout << factorial_calculated << std::endl;


#### Task throws an exception


```C++
void throws()
{
    boost::this_fiber::sleep_for(3s);

    // task wil throw an exception 
    throw std::runtime_error("Exception thrown in " 
                             "a task running in fiber " 
                             "in the pool");
}

auto future_3 = DefaultFiberPool::submit_job(throws);

//
// do other things here if needed, e.g., submit second fiber task
//

// get exception pointer to check if the task thrown something
std::exception_ptr exp_ptr {future_3.get_exception_ptr()};

if (exp_ptr)
{
    // if exception was thrown, rethrow it
    std::rethrow_exception(exp_ptr);
}

```


## How can you help?

Constructive criticism, issues and code fixes/improvements are welcomed.
