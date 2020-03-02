/**
 * Implementation of the BoundedBuffer class.
 * See the associated header file (BoundedBuffer.hpp) for the declaration of
 * this class.
 */
#include <cstdio>

#include "BoundedBuffer.hpp"

unsigned int capacity;
std::queue<int> buffer;

/**
 * Constructor that sets capacity to the given value. The buffer itself is
 * initialized to en empty queue.
 *
 * @param max_size The desired capacity for the buffer.
 */
BoundedBuffer::BoundedBuffer(int max_size) {
	capacity = max_size;
	
	// buffer field implicitly has its default (no-arg) constructor called.
	// This means we have a new buffer with no items in it.
}

/**
 * Gets the first item from the buffer then removes it.
 */
int BoundedBuffer::getItem() {
	std::unique_lock<std::mutex> spaceAva(m);
	while(this->buffer.size() == 0)
	{
		cv.wait(spaceAva);
	}
	int item = this->buffer.front(); // "this" refers to the calling object...
	this->buffer.pop(); // ... but like Java it is optional (no this in front of buffer on this line)
	spaceAva.unlock();
	cv.notify_all();
	return item;
}

/**
 * Adds a new item to the back of the buffer.
 *
 * @param new_item The item to put in the buffer.
 */
void BoundedBuffer::putItem(int new_item) {
	std::unique_lock<std::mutex> dataAva(m);
	while(this->buffer.size() == (long unsigned int) capacity)
	{
		cv.wait(dataAva);
	}
	this->buffer.push(new_item);
	dataAva.unlock();
	cv.notify_all();
}

bool isFull()
{
	return buffer.size() >= capacity;
}

bool isEmpty()
{
	return buffer.size() == 0;
}
