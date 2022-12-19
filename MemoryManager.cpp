#include "MemoryManager.h"


void MemoryManager::load(Page *p)
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

void MemoryManager::unload(Page *p)
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

auto MemoryManager::get_pages(std::thread::id id, GET_MODE get_mode) -> std::vector<Page*>
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

char* MemoryManager::allocate_new(std::size_t size)
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
            return p->allocate(size);
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
                    // unreachable???
                    Page *new_page = createPage(pid);
                    loaded_pages_amount += 1;
                    return new_page->allocate(size);
                }
            }
            load(p);
            return p->allocate(size);
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
    Page *new_page = createPage(pid);
    loaded_pages_amount += 1;
    return new_page->allocate(size);
}

// creates a new page for specified process and adds it to pages vector
Page* MemoryManager::createPage(const std::thread::id &pid) {
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
    return p;
}

void MemoryManager::unload_process()
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

void MemoryManager::free_process()
{
    std::unique_lock lock(get_mutex());

    auto pid = std::this_thread::get_id();
    for (auto iter = pages.begin(); iter != pages.end(); ) {
        if ((*iter)->pid == pid)
        {
            iter = pages.erase(iter);
        }
        else
        {
            ++iter;
        }

    }
}

void MemoryManager::free_all()
{
    for (auto iter = pages.begin(); iter != pages.end(); ) {
        iter = pages.erase(iter);
    }
}

void MemoryManager::print_pages(bool order_by_pid)
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

