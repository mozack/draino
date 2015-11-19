#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>

// Buffer up to .5 GB
#define PAGE_SIZE 4096
#define NUM_PAGES 131072

char* buf;

struct page_queue {
	char* pages[NUM_PAGES];
	pthread_mutex_t lock;
	int front;
	int size;
};

page_queue buffered_pages;
page_queue available_pages;

char is_done;

char is_page_available(page_queue& q) {
	char is_available = 0;

	pthread_mutex_lock(&q.lock);
	is_available = q.size > 0;
	pthread_mutex_unlock(&q.lock);

	return is_available;
}

char* dequeue(page_queue& q) {
	char* page = NULL;

	while (!is_page_available(q) && !is_done) {
		// No page ready / available.  Sleep for 10 milliseconds
		usleep(10*1000);
	}

	if (is_page_available(q)) {
		pthread_mutex_lock(&(q.lock));
		page = q.pages[q.front];
		q.front += 1;
		if (q.front == NUM_PAGES) {
			q.front = 0;
		}
		q.size -= 1;
		pthread_mutex_unlock(&(q.lock));
	}

	return page;
}

void enqueue(page_queue& q, char* page) {
	pthread_mutex_lock(&(q.lock));
	if (q.size == 0) {
		q.pages[0] = page;
		q.front = 0;
		q.size = 1;
	} else {
		int back = q.front + q.size;
		if (back >= NUM_PAGES) {
			back -= NUM_PAGES;
		}

		q.pages[back] = page;
		q.size += 1;
	}
	pthread_mutex_unlock(&(q.lock));
}

void init() {
	is_done = 0;
	pthread_mutex_init(&buffered_pages.lock, NULL);
	pthread_mutex_init(&available_pages.lock, NULL);
	buf = (char*) calloc(PAGE_SIZE * NUM_PAGES, 1);

	char* page = buf;

	for (int i=0; i<NUM_PAGES; i++) {
		available_pages.pages[i] = page;
		page += PAGE_SIZE;
	}

	available_pages.front = 0;
	available_pages.size = NUM_PAGES;
	buffered_pages.front = 0;
	buffered_pages.size = 0;
}

int read_page(char* page) {
	return fread(page, 1, PAGE_SIZE, stdin);
}

void write_page(char* page, size_t size) {
	int count = fwrite(page, 1, size, stdout);
	if (count != size) {
		fprintf(stderr, "Draino error writing to stdout: %d\n", count);
	}
}

void write_page(char* page) {
	write_page(page, PAGE_SIZE);
}

void* writer_thread(void* t) {

	while (!is_done || is_page_available(buffered_pages)) {
		char* page = dequeue(buffered_pages);

		if (page != NULL) {
			// Write page and return to available queue
			write_page(page);
			enqueue(available_pages, page);
		}
	}

	fflush(stdout);
	pthread_exit(NULL);
}

void run() {
	init();

	// Spawn writer thread
	pthread_t writer_t;
	int ret = pthread_create(&writer_t, NULL, writer_thread, NULL);

	if (ret != 0) {
		fprintf(stderr, "Draino error spawning writer thread: %d\n", ret);
		exit(ret);
	}

	// Read first page
	char* page = dequeue(available_pages);
	int count = read_page(page);

	// Send pages to the output buffer and continue reading until no more pages to read.
	while (count == PAGE_SIZE) {
		enqueue(buffered_pages, page);
		page = dequeue(available_pages);
		count = read_page(page);
	}

	// Signal writer thread and wait for it to finish.
	is_done = 1;
	ret = pthread_join(writer_t, NULL);
	if (ret != 0) {
		fprintf(stderr, "Draino error joining writer thread: %d\n", ret);
		exit(ret);
	}

	// Output any leftovers
	if (count > 0 && count < PAGE_SIZE) {
		write_page(page, count);
	}

	pthread_mutex_destroy(&(buffered_pages.lock));
	pthread_mutex_destroy(&(available_pages.lock));

	free(buf);
}

int main(int argc, char** argv) {
	run();
}

