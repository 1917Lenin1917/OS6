#include "MemoryManager.h"
#include <csignal>


void* operator new(size_t size, MemoryManager& m)
{
    return m.allocate_new(size);
}

#define new_m(type) new (MemoryManager::get()) (type)
#define deref_m(address) (MemoryManager::get().get_value<decltype(address)>(address))
#define free_m(address) (MemoryManager::get().free_value<decltype(address)>(address))

auto worker(int it_count)
{
    
    using namespace std::chrono_literals;
    std::vector<int> result;

    std::random_device rnd;
    std::uniform_int_distribution dst(1, 100);
	try
    {
        for (int i = 0; i < it_count; i++)
        {
            int* p = new_m(int);
            deref_m(p) = dst(rnd);
            result.push_back(deref_m(p));
        }
    }
    catch (const page_fault& e)
    {
        std::cout << e.what() << "\n";
        raise(SIGTERM);
    }
    //MemoryManager::get().unload_process();
    //MemoryManager::get().free_process();
    std::this_thread::sleep_for(5s);
    return result;
}



int main()
{
    try
    {
        std::vector<std::future<std::vector<int>>> futures(20);
        for (auto i = 0; i < 20; i++)
        {
            futures[i] = std::async(worker, 100);
            //futures[i] = std::async(worker, 1000);
        }
        for (auto& f : futures)
        {
            auto v = f.get();
            std::cout << "Worker returned: " << "something..." << "\n";
        }
        auto m = MemoryManager::get();
        m.print_pages(true);
        //std::cin.get();
    }
    catch (const page_fault& e)
    {
        std::cout << e.what() << "\n";

    	auto m = MemoryManager::get();
        m.print_pages(true);
    }
}