#include <algorithm>
#include <new>
#include <iostream>
#include <vector>
#include <string>
#include <string_view>
#include <thread>
#include <fstream>
#include <sstream>
#include <chrono>
#include <future>
#include <iomanip>
#include <mutex>
#include <random>


#define KB(x)   ((size_t) (x) << 10)
#define MB(x)   ((size_t) (x) << 20)

template <class T>
std::string to_string(T t, std::ios_base& (*f)(std::ios_base&))
{
    std::ostringstream oss;
    oss << f << static_cast<void*>(t);
    return oss.str();
}

class page_fault : public std::bad_alloc
{
    std::string msg;
public:
    page_fault(const std::string& v)
	    :msg("Page fault: " + v) { }

    char const* what() const override
    {
        return msg.c_str();
    }
};

struct Page
{
    const size_t SIZE = KB(1);
    size_t free_space = KB(1);

	bool loaded = false;

	char* start_address = nullptr; // virtual start address 
    char* free_address  = nullptr; // virtual free address 

    std::vector<char*> free_addresses;

    char* data = nullptr;          // actual blob of data

	std::string page_name;
    std::thread::id pid;

    friend std::ostream& operator<< (std::ostream& out, const Page& p)
    {
        std::vector<int> colors = { 31, 32, 33, 34, 35, 36, 37, 90, 91, 92, 93, 94, 95, 96, 97 };
        std::uniform_int dist(31, 37);
        auto color = std::hash<std::thread::id>()(p.pid) % colors.size();
        out << "Page: { ";
        out << "loaded: " << std::setw(3) << (p.loaded ? "Yes" : "no") << " | ";
        out << "space left = " << std::setw(4) << p.free_space << "b | ";
        out << "virtual address = 0x" << to_string(p.start_address, std::hex) << " | ";
        out << "real address = 0x" << to_string(p.data, std::hex) << " | ";
        out << "pid = \x1B["<< colors[color] << "m" << std::setw(5) << p.pid << "\033[0m";
    	out << " }";

        return out;
    }
};

class MemoryManager
{
    enum GET_MODE
    {
        ALL,
        LOADED_ONLY,
        STORED_ONLY
    };

    static MemoryManager* _instance;

    const uint16_t MAX_LOADED_PAGES = 16;
    const uint16_t MAX_PAGES        = 64;

    uint16_t loaded_pages_amount = 0;
    uint16_t stored_pages_amount = 0;

    char* new_page_start_address = reinterpret_cast<char*>(1000);

    std::vector<Page*> pages;

    void load(Page* p)
    {
        std::ifstream fin{ p->page_name };
        if (!fin.is_open())
            throw page_fault{ "Page with name " + p->page_name + " cannot be found on the disk." };

        p->data = new char[p->SIZE];
        fin.read(p->data, p->SIZE);
        p->loaded = true;

        loaded_pages_amount += 1;
        stored_pages_amount -= 1;
    }
    void unload(Page* p)
    {
        if (!p->loaded)
            return;
        std::ofstream fout{ p->page_name };
        fout.write(p->data, p->SIZE);
        delete p->data;
        p->data = nullptr;
        p->loaded = false;

        loaded_pages_amount -= 1;
        stored_pages_amount += 1;
    }
    static std::mutex& get_mutex()
    {
        return m_mutex;
    }
    std::vector<Page*> get_pages(std::thread::id id, GET_MODE get_mode = ALL)
    {
        std::vector<Page*> retval;
        for (auto p : pages)
        {
            if (p->pid == id)
            {
                if (get_mode == LOADED_ONLY && p->loaded)
                    retval.push_back(p);
                if (get_mode == STORED_ONLY && !p->loaded)
                    retval.push_back(p);
                if (get_mode == ALL)
                    retval.push_back(p);
            }
        }
        return retval;
    }


public:
    static std::mutex m_mutex;
    
 
    static MemoryManager& get()
    {
        static MemoryManager m;
        return m;
    }
    
    char* allocate_new(std::size_t size)
    {
        std::unique_lock lock(get_mutex());
        auto pid = std::this_thread::get_id();
        //std::cout << pid << "\n";

        auto loaded_pages = get_pages(pid, LOADED_ONLY);
        auto stored_pages = get_pages(pid, STORED_ONLY);

        for (auto p : loaded_pages)
        {
	        if (p->free_space >= size)
	        {
                auto retval = p->free_address;
                p->free_address += size;
                p->free_space -= size;
                return retval;
	        }
        }
        for (auto p : stored_pages)
        {
            if (p->free_space > size)
            {
                if (loaded_pages_amount >= MAX_LOADED_PAGES)
                {
                    bool flag = false;
                    // try to unload page from another thread
	                for (auto other_p : pages)
	                {
		                if (other_p->pid != p->pid)
		                {
                            unload(other_p);
                            flag = true;
                            break;
		                }
	                }
                    // if couldn't unload page from another thread, unload page from this thread
                    if (!flag && !loaded_pages.empty())
                    {
                        unload(loaded_pages[0]);
                    }
                    else
                    {

                        auto p = new Page;
                        p->pid = pid;
                        p->page_name = std::to_string(std::hash<std::thread::id>{}(std::this_thread::get_id())) + " " + std::to_string((int)&new_page_start_address);
                        p->start_address = new_page_start_address;
                        p->free_address = p->start_address;
                        new_page_start_address += p->SIZE;

                        pages.push_back(p);
                        p->loaded = true;

                        // initialize blob of data and fill it with zeros;
                        p->data = new char[p->SIZE];
                        memset(p->data, 0, p->SIZE);
                        auto retval = p->free_address;
                        p->free_address += size;
                        p->free_space -= size;
                        loaded_pages_amount += 1;
                        return retval;
                    }
                }
                load(p);
            	auto retval = p->free_address;
                p->free_address += size;
                p->free_space -= size;
                return retval;
            }
        }

        if (loaded_pages_amount >= MAX_LOADED_PAGES)
        {
            bool flag = false;
            // try to unload page from another thread
            for (auto other_p : pages)
            {
                if (other_p->loaded && other_p->pid != pid)
                {
                    unload(other_p);
                    flag = true;
                    break;
                }
            }
            // if couldn't unload page from another thread, unload page from this thread
            if (!flag)
            {
                unload(loaded_pages[0]);
            }
        }
        if (pages.size() >= MAX_PAGES)
            throw page_fault{ "Cannot create a new page. Max limit reached." };
        auto p = new Page;
        p->pid = pid;
        p->page_name = std::to_string(std::hash<std::thread::id>{}(std::this_thread::get_id())) + " " + std::to_string((int)&new_page_start_address);
        p->start_address = new_page_start_address;
        p->free_address = p->start_address;
        new_page_start_address += p->SIZE;

        pages.push_back(p);
        p->loaded = true;

        // initialize blob of data and fill it with zeros;
        p->data = new char[p->SIZE];
        memset(p->data, 0, p->SIZE);
        auto retval = p->free_address;
        p->free_address += size;
        p->free_space -= size;
        loaded_pages_amount += 1;
        return retval;
    }

    template <class Pointer>
    std::remove_pointer_t<Pointer>& get_value(Pointer address)
    {
        std::unique_lock lock(get_mutex());

    	typedef std::remove_pointer_t<Pointer> RetType;
        auto char_address = reinterpret_cast<char*>(address);
        for (auto p : pages)
        {
	        if (p->start_address <= char_address && char_address < p->start_address + p->SIZE)
	        {
                if (p->free_address < char_address)
                    throw page_fault("Access violation at address: 0x" + to_string((char_address), std::hex));

                if (!p->loaded)
                {
                    if (loaded_pages_amount >= MAX_LOADED_PAGES)
                    {
                        bool flag = false;
                        // try to unload page from another thread
                        for (auto other_p : pages)
                        {
                            if (other_p->pid != p->pid)
                            {
                                unload(other_p);
                                flag = true;
                                break;
                            }
                        }
                        // if couldn't unload page from another thread, unload page from this thread
                        if (!flag)
                        {
                            for (auto op : pages)
                            {
                                if (op->loaded)
                                {
									unload(op);
                                    break;
                                }
                            }
                        }
                    }

					load(p);
                }
                auto addr = (int)address - (int)p->start_address;
                return (RetType&)p->data[addr];
	        }
        }
        throw page_fault("Access violation at address: 0x" + to_string((char_address), std::hex));
    }

    template <class Pointer>
    void free_value(Pointer address)
    {
        std::unique_lock lock(get_mutex());

        auto char_address = reinterpret_cast<char*>(address);
        for (auto p : pages)
        {
            if (p->start_address <= char_address && char_address < p->start_address + p->SIZE)
            {
                if (p->free_address < char_address)
                    throw page_fault("Access violation at address: 0x" + to_string((char_address), std::hex));

                if (!p->loaded)
                {
                    if (loaded_pages_amount >= MAX_LOADED_PAGES)
                    {
                        bool flag = false;
                        // try to unload page from another thread
                        for (auto other_p : pages)
                        {
                            if (other_p->pid != p->pid)
                            {
                                unload(other_p);
                                flag = true;
                                break;
                            }
                        }
                        // if couldn't unload page from another thread, unload page from this thread
                        if (!flag)
                        {
                            for (auto op : pages)
                            {
                                if (op->loaded)
                                {
                                    unload(op);
                                    break;
                                }
                            }
                        }
                    }

                    load(p);
                }
                // ... implement free vector
                
            }
        }
        throw page_fault("Access violation at address: 0x" + to_string((char_address), std::hex));
    }

    void unload_process()
    {
        std::unique_lock lock(get_mutex());

        auto pid = std::this_thread::get_id();
	    for (auto p : pages)
	    {
		    if (p->pid == pid)
		    {
                unload(p);
		    }
	    }
    }

    void free_process()
    {
        std::unique_lock lock(get_mutex());

        auto pid = std::this_thread::get_id();
        for (auto iter = pages.begin(); iter != pages.end(); ) {
            if ((*iter)->pid == pid)
            {
                delete (*iter)->data;
            	iter = pages.erase(iter);
	            
            }
            else
                ++iter;
        }
    }

    void free_all()
    {
        for (auto iter = pages.begin(); iter != pages.end(); ) {
            iter = pages.erase(iter);
        }
    }

    void print_pages(bool order_by_pid = false)
    {
        auto it_pages = pages;
        if (it_pages.empty())
        {
            std::cout << "There are no pages!\n";
            return;
        }
        std::cout << "Total page amount: " << it_pages.size() << "\n";
        if (order_by_pid)
        {
            std::sort(it_pages.begin(), it_pages.end(), [](Page* page1, Page* page2) { return page1->pid < page2->pid; });
        }
	    for (auto p : it_pages)
	    {
            std::cout << *p << "\n";
	    }
    }
};

std::mutex MemoryManager::m_mutex;

void* operator new(size_t size, MemoryManager& m)
{
    return m.allocate_new(size);
}

#define new_m(type) new (MemoryManager::get()) (type)
#define deref(address) (MemoryManager::get().get_value<decltype(address)>(address))
#define free_m(address) (MemoryManager::get().free_value<decltype(address)>(address))

std::ostream& operator<<(std::ostream& out, std::vector<int> v)
{
    for (const auto& value : v)
    {
        out << value << " ";
    }
    return out;
}

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
            deref(p) = dst(rnd);
            result.push_back(deref(p));
        }
    }
    catch (const page_fault& e)
    {
        std::cout << e.what() << "\n";
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