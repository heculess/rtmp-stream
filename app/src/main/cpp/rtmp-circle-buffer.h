
#pragma once

#include "util/circlebuf.h"
#include <stdint.h>
#include <vector>

#ifdef __cplusplus
extern "C" {
#endif


class circlebuffer : public circlebuf{
public:
	circlebuffer();
	virtual  ~circlebuffer();

	void peek_front(void *data, size_t size);
    void push_back(const void *data,  size_t size);
    void pop_front(void *data,  size_t size);

    void *get_data(size_t idx);
    void reserve(size_t capacity);
    void free();
private:

};

#ifdef __cplusplus
}
#endif
