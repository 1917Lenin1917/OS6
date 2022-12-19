#pragma once

#include <new>
#include <random>

#include <iostream>
#include <iomanip>
#include <fstream>
#include <sstream>

#include <string>
#include <vector>
#include <algorithm>

#include <thread>
#include <mutex>
#include <future>

#include "Utility.h"


class page_fault : public std::bad_alloc
{
    std::string msg;
public:
    explicit page_fault(const std::string& v)
            :msg("Page fault: " + v) { }

    [[nodiscard]] char const* what() const override
    {
        return msg.c_str();
    }
};


class Page
{
public:
    const size_t SIZE = KB(1);
    size_t free_space = KB(1);

    bool loaded = false;

    char* start_address = nullptr; // virtual start address
    char* free_address  = nullptr; // virtual free address

    std::vector<char*> free_addresses;

    char* data = nullptr;          // actual blob of data

    std::string page_name;
    std::thread::id pid;

    char* allocate(std::size_t size)
    {
        auto retval = free_address;
        free_address += size;
        free_space -= size;
        return retval;
    }

    Page() = default;
    ~Page()
    {
        delete[] data;
    }
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

    void load(Page* p);
    void unload(Page* p);
    auto get_pages(std::thread::id id, GET_MODE get_mode = ALL) -> std::vector<Page*>;

public:
    char* allocate_new(std::size_t size);
    Page *createPage(const std::thread::id &pid);

    template <class Pointer>
    auto get_value(Pointer address) -> std::remove_pointer_t<Pointer>&
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

    void unload_process();
    void free_process();
    void free_all();

    void print_pages(bool order_by_pid = false);

    static std::mutex& get_mutex()
    {
        static std::mutex m_mutex;
        return m_mutex;
    }

    static MemoryManager& get()
    {
        static MemoryManager instance;
        return instance;
    }
};

