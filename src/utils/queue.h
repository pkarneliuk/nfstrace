//------------------------------------------------------------------------------
// Author: Pavel Karneliuk
// Description: Special Queue for fixed size elements without copying them
// Copyright (c) 2013 EPAM Systems
//------------------------------------------------------------------------------
/*
    This file is part of Nfstrace.

    Nfstrace is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, version 2 of the License.

    Nfstrace is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with Nfstrace.  If not, see <http://www.gnu.org/licenses/>.
*/
//------------------------------------------------------------------------------
#ifndef QUEUE_H
#define QUEUE_H
//------------------------------------------------------------------------------
#include <cstddef>
#include <memory>
#include <type_traits>

#include "utils/block_allocator.h"
#include "utils/noncopyable.h"
#include "utils/spinlock.h"
//------------------------------------------------------------------------------
namespace NST
{
namespace utils
{
template <typename T>
class Queue final : noncopyable
{
    struct Element final : noncopyable
    {
        Element* prev;
        T        data;
    };

    struct ElementDeleter final
    {
        explicit ElementDeleter(Queue* q = nullptr) noexcept
            : queue{q}
        {
        }
        void operator()(T* const pointer) const
        {
            if(pointer /*&& queue - dont check - optimization*/)
            {
                queue->deallocate(pointer);
            }
        }

        Queue* queue;
    };

public:
    using Ptr = std::unique_ptr<T, ElementDeleter>;

    class List final : noncopyable
    {
    public:
        inline explicit List(Queue& q)
            : queue{&q}
        {
            ptr = queue->pop_list();
        }
        ~List()
        {
            while(ptr)
            {
                free_current();
            }
        }

        operator bool() const { return ptr; }       // is empty?
        const T& data() const { return ptr->data; } // get data
        Ptr      get_current()                      // return element and switch to next
        {
            Element* tmp{ptr};
            ptr = ptr->prev;
            return Ptr{&tmp->data, ElementDeleter{queue}};
        }
        void free_current() // deallocate element and switch to next
        {
            Element* tmp{ptr->prev};
            queue->deallocate(ptr);
            ptr = tmp;
        }

    private:
        Element* ptr;
        Queue*   queue;
    };

    Queue(uint32_t size, uint32_t limit)
        : last{nullptr}
        , first{nullptr}
    {
        allocator.init_allocation(sizeof(Element), size, limit);
    }
    ~Queue()
    {
        List list{*this}; // deallocate items by destructor of List
    }

    T* allocate()
    {
        static_assert(std::is_nothrow_constructible<T>::value,
                      "The construction of T must not to throw any exception");

        Spinlock::Lock lock{a_spinlock};
        Element*       e{(Element*)allocator.allocate()}; // may throw std::bad_alloc
        return ::new(&(e->data)) T;                       // placement construction T
    }

    void deallocate(T* ptr)
    {
        ptr->~T(); // placement construction was used
        Element* e{(Element*)(((char*)ptr) - offsetof(Element, data))};
        deallocate(e);
    }

    void push(T* ptr)
    {
        Element*       e{(Element*)(((char*)ptr) - offsetof(Element, data))};
        Spinlock::Lock lock{q_spinlock};
        if(last)
        {
            last->prev = e;
            last       = e;
        }
        else // queue is empty
        {
            last = first = e;
        }
    }

    Element* pop_list() // take out list of all queued elements
    {
        Element* list{nullptr};
        if(last)
        {
            Spinlock::Lock lock{q_spinlock};
            if(last)
            {
                list       = first;
                last->prev = nullptr; // set end of list
                last = first = nullptr;
            }
        }
        return list;
    }

private:
    // accessible from Queue::List and Queue::Ptr
    void deallocate(Element* e)
    {
        Spinlock::Lock lock{a_spinlock};
        allocator.deallocate(e);
    }

    BlockAllocator allocator;
    Spinlock       a_spinlock; // for allocate/deallocate
    Spinlock       q_spinlock; // for queue push/pop

    // queue empty:   last->nullptr<-first
    // queue filled:  last->e<-e<-e<-e<-first
    // queue push(i): last->i<-e<-e<-e<-e<-first
    Element* last;
    Element* first;
};

} // namespace utils
} // namespace NST
//------------------------------------------------------------------------------
#endif // QUEUE_H
//------------------------------------------------------------------------------
