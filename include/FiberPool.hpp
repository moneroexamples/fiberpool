// FiberPool based on:
// http://roar11.com/2016/01/a-platform-independent-thread-pool-using-c14/

#pragma once

#include <boost/fiber/all.hpp> 

	
namespace FiberPool
{

inline auto 
no_of_defualt_threads()
{
    return std::max(std::thread::hardware_concurrency(), 2u) - 1u;
}


template <typename BaseChannel>
class TaskQueue 
{
public:
    using value_type = typename BaseChannel::value_type;

    explicit TaskQueue(std::size_t capacity)
        : m_base_channel {capacity}
    {}

    TaskQueue(const TaskQueue& rhs) = delete;
    TaskQueue& operator=(TaskQueue const& rhs) = delete;

    TaskQueue(TaskQueue&& other) = default;
    TaskQueue& operator=(TaskQueue&& other) = default;

    virtual ~TaskQueue() = default;

    boost::fibers::channel_op_status 
    push(typename BaseChannel::value_type const& value)
    {
        return m_base_channel.push(value);
    }
    
    boost::fibers::channel_op_status 
    push(typename BaseChannel::value_type&& value)
    {
        return m_base_channel.push(std::move(value));
    }
    
    boost::fibers::channel_op_status 
    pop(typename BaseChannel::value_type& value)
    {
        return m_base_channel.pop(value);
    }

    void close() noexcept 
    {
        m_base_channel.close();
    }

private:
    BaseChannel m_base_channel; 
};


class IFiberTask
{
public:

    // how many running fibers there are
    inline static std::atomic<size_t> no_of_fibers {0};

    IFiberTask(void) = default;

    virtual ~IFiberTask(void) = default;
    IFiberTask(const IFiberTask& rhs) = delete;
    IFiberTask& operator=(const IFiberTask& rhs) = delete;
    IFiberTask(IFiberTask&& other) = default;
    IFiberTask& operator=(IFiberTask&& other) = default;

    /**
     * Run the task.
     */
    virtual void execute() = 0;
};

template<
    template<typename> typename task_queue_t 
        = boost::fibers::buffered_channel,
    typename work_task_t = std::unique_ptr<IFiberTask>
>
class FiberPool
{
private:


    /**
     * A wrapper for packaged fiber task
     */
    template <typename Func>
    class FiberTask: public IFiberTask
    {
    public:
        FiberTask(Func&& func)
            :m_func{std::move(func)}
        {}

        ~FiberTask(void) override = default;
        FiberTask(const FiberTask& rhs) = delete;
        FiberTask& operator=(const FiberTask& rhs) = delete;
        FiberTask(FiberTask&& other) = default;
        FiberTask& operator=(FiberTask&& other) = default;

        /**
         * Run the task.
         */
        void execute() override
        {
			++no_of_fibers;
            m_func();
			--no_of_fibers;
        }

    private:
        Func m_func;
    }; 

public:

    FiberPool()
        : FiberPool {no_of_defualt_threads()}
    {}

    explicit FiberPool(
            size_t no_of_threads,
            size_t work_queue_size = 32)
		: m_work_queue {work_queue_size}
    {
        try 
        {
            for(std::uint32_t i = 0; i < no_of_threads; ++i)
            {
                m_threads.emplace_back(&FiberPool::worker, this);
            }
        }
        catch(...)
        {
            close_queue();
            throw;
        }
    }

    /**
     * Submit a task to be executed as fiber by worker threads
     */
    template<typename Func, typename... Args>
    auto submit(Func&& func, Args&&... args)
    {
        // capature our task into lambda with all its parameters
        auto capture = [func = std::forward<Func>(func),
                        args = std::make_tuple(std::forward<Args>(args)...)]() 
                            mutable
            {
                // run the tesk with the parameters provided
                // this will be what our fibers execute
                return std::apply(std::move(func), std::move(args));
            };

        // get return type of our task
        using task_result_t = std::invoke_result_t<decltype(capture)>;

        // create fiber package_task
        using packaged_task_t 
            = boost::fibers::packaged_task<task_result_t()>; 
        
        packaged_task_t task {std::move(capture)};
        
        using task_t = FiberTask<packaged_task_t>;

        // get future for obtaining future result when 
        // the fiber completes
        auto result_future = task.get_future();

        // finally submit the packaged task into work queue
        auto status = m_work_queue.push(
                std::make_unique<task_t>(std::move(task)));

        if (status != boost::fibers::channel_op_status::success)
        {
            return std::optional<std::decay_t<decltype(result_future)>> {};
        }

        // return the future to the caller so that 
        // we can get the result when the fiber with our task 
        // completes
        return std::make_optional(std::move(result_future));
    }

    /**
     * Non-copyable.
     */
    FiberPool(FiberPool const& rhs) = delete;

    /**
     * Non-assignable.
     */
    FiberPool& operator=(FiberPool const& rhs) = delete;

    void close_queue() noexcept 
    {
        m_work_queue.close();
    }

    auto threads_no() const noexcept
    {
        return m_threads.size();
    }

	auto fibers_no() const noexcept
	{
		return IFiberTask::no_of_fibers.load();
	}
   
	~FiberPool()
	{
		for(auto& thread : m_threads)
		{
			 if(thread.joinable())
			 {
				 thread.join();
			 }
		}
	}

private:

    /**
     * worker thread method. It participates with 
     * shared_work sheduler of fibers.
     *
     * It takes packaged taskes from the work_queue
     * and launches fibers executing the tasks
     */
    void worker()
    {
        // make this thread participate in shared_work 
        // fiber sharing
        //
        // We use "shared_work" sheduler so that the work 
        // is distributed equally among all threads

        boost::fibers::use_scheduling_algorithm<
            boost::fibers::algo::shared_work>();
        
        // create a placeholder for packaged task for 
        // to-be-created fiber to execute
        auto packaged_search_task 
            = typename decltype(m_work_queue)::value_type {}; 

        // fetch a packaged task from the work queue.
        // if there is nothing, we are just going to wait
        // here till we get some task
        while(boost::fibers::channel_op_status::success 
                == m_work_queue.pop(packaged_search_task))
        {
            // creates a fiber from the pacakged task.
            //
            // the fiber is immedietly detached so that we
            // fetch next task from the queue without blocking
            // the thread and waiting here for the fiber to 
            // complete
            //
            // earlier we already got future for the fiber
            // so we can get the result of our task if we want
            boost::fibers::fiber(
                    [task = std::move(packaged_search_task)]()
                    {
                        // execute our task in the newly created
                        // fiber
                        task->execute();
                    }).detach();
        }
    }

    // worker threads. these are the threads which will 
    // be executing our fibers. Since we use work_shearing scheduling
    // algorithm, the fibers will be shared between these threads
    std::vector<std::thread> m_threads;
    
    // use buffered_channel so that we dont block when there is no 
    // reciver for the fiber. we are only going to block when
    // the buffered_channel is full. Otherwise, tasks will be just
    // waiting in the queue till some fiber picks them up.
    
    TaskQueue<task_queue_t<work_task_t>> m_work_queue;
};

}

namespace DefaultFiberPool
{

inline auto& 
get_pool()
{
    static FiberPool::FiberPool default_fp {};
    return default_fp;
};


template <typename Func, typename... Args>
inline auto 
submit_job(Func&& func, Args&&... args)
{
	return get_pool().submit(std::forward<Func>(func), 
							 std::forward<Args>(args)...);
}

void
close()
{
	get_pool().close_queue();
}

}
