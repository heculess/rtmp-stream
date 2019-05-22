
#include "rtmp-struct.h"
#include "util/platform.h"

media_output::media_output():
update_semaphore(NULL),
initialized(false),
stop(false)
{
    initialized = false;
    pthread_mutexattr_t attr;
    if (pthread_mutexattr_init(&attr) != 0)
        return;
    if (pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE) != 0)
        return;
    if (pthread_mutex_init(&data_mutex, &attr) != 0)
        return;
    if (pthread_mutex_init(&input_mutex, &attr) != 0)
        return;
    if (os_sem_init(&update_semaphore, 0) != 0)
        return;

    initialized = true;
}

media_output::~media_output()
{
    output_close();
    os_sem_destroy(update_semaphore);
    pthread_mutex_destroy(&data_mutex);
    pthread_mutex_destroy(&input_mutex);
}

void *media_output::media_thread(void *param)
{
    media_output *media = (media_output *)param;
    if(!media)
        return NULL;

    media->media_thread_run();

    return NULL;
}

void media_output::media_thread_run()
{
    on_media_thread_create();

    while (os_sem_wait(update_semaphore) == 0) {
        if (stop)
            break;

        while (!stop) {
            if(output_cur_frame())
                break;
        }
    }
}

bool media_output::output_open()
{
    stop = false;
	if (pthread_create(&thread, NULL, media_thread, this) != 0)
		return false;
	return true;
}

void media_output::output_close()
{
    if (initialized) {
        stop = true;
        os_sem_post(update_semaphore);
        void *thread_ret = NULL;
        pthread_join(thread, &thread_ret);
    }
}

void media_output::update_input_frame(media_data &input_frame)
{
    if(stop)
        return;

    pthread_mutex_lock(&data_mutex);
    cache = input_frame;
    pthread_mutex_unlock(&data_mutex);

    output_unlock_frame();
}

bool media_output::output_cur_frame()
{
    media_data frame;
    pthread_mutex_lock(&data_mutex);
    frame = cache;
    pthread_mutex_unlock(&data_mutex);

    pthread_mutex_lock(&input_mutex);
    on_input_mutex(frame);
    pthread_mutex_unlock(&input_mutex);

    return true;
}

void media_output::output_unlock_frame()
{
    pthread_mutex_lock(&data_mutex);
    os_sem_post(update_semaphore);
    pthread_mutex_unlock(&data_mutex);
}






