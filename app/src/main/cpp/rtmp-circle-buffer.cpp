
#include "rtmp-circle-buffer.h"

circlebuffer::circlebuffer()
{
    data = NULL;
    size = 0;
    start_pos = 0;
    end_pos = 0;
    capacity = 0;
}

circlebuffer::~circlebuffer()
{
    free();
}

void circlebuffer::peek_front(void *data, size_t size)
{
    circlebuf_peek_front(dynamic_cast<circlebuf *>(this),data,size);
}

void circlebuffer::push_back(const void *data,  size_t size)
{
    circlebuf_push_back(dynamic_cast<circlebuf *>(this), data, size);
}

void circlebuffer::pop_front(void *data,  size_t size)
{
    circlebuf_pop_front(dynamic_cast<circlebuf *>(this), data, size);
}

void *circlebuffer::get_data(size_t idx)
{
    return circlebuf_data(dynamic_cast<circlebuf *>(this),idx);
}

void circlebuffer::reserve(size_t capacity)
{
    circlebuf_reserve(dynamic_cast<circlebuf *>(this),capacity);
}

void circlebuffer::free()
{
    circlebuf_free(dynamic_cast<circlebuf *>(this));
}





