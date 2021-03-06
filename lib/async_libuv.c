#define NO_OLDNAMES
// to get pass some problem about mkdir and macros
#include "async_impl.h"
#include "smemory.h"
#include <uv.h>

struct LwqqAsyncTimer_ {
   LwqqAsyncTimer super;
   uv_timer_t h;
};
struct LwqqAsyncIo_ {
   LwqqAsyncIo super;
   uv_poll_t h;
};
static uv_loop_t* loop = NULL;
static void(loop_create)()
{
   if (loop)
      return;
   loop = uv_loop_new();
}
static void(loop_run)() { uv_run(loop, UV_RUN_DEFAULT); }

static void loop_stop_cb(uv_idle_t* idle, int action)
{
   uv_idle_stop(idle);
   s_free(idle);
   uv_stop(loop);
}

static void(loop_stop)()
{
   uv_stop(loop);
   uv_idle_t* idle = s_malloc0(sizeof(*idle));
   uv_idle_init(loop, idle);
   uv_idle_start(idle, loop_stop_cb);
}
static void(loop_free)()
{
   uv_loop_delete(loop);
   loop = NULL;
}
static void*(io_new)() { return s_malloc0(sizeof(struct LwqqAsyncIo_)); }

static void(io_free)(void* h) { s_free(h); }
static void io_cb_wrap(uv_poll_t* w, int status, int action)
{
   // if(global_quit_lock) return;
   struct LwqqAsyncIo_* io_ = w->data;
   if (io_->super.func)
      io_->super.func(w->data, io_->super.fd, action, io_->super.data);
}
static void(io_start)(void* io, int fd, int action)
{
   struct LwqqAsyncIo_* io_ = (struct LwqqAsyncIo_*)io;
   io_->h.data = io;
   uv_poll_init(loop, &io_->h, fd);
   uv_poll_start(&io_->h, action, io_cb_wrap);
}

static void(io_stop)(void* io)
{
   struct LwqqAsyncIo_* io_ = (struct LwqqAsyncIo_*)io;
   uv_poll_stop(&io_->h);
}

static void*(timer_new)() { return s_malloc0(sizeof(struct LwqqAsyncTimer_)); }

static void(timer_free)(void* t) { s_free(t); }

static void timer_cb_wrap(uv_timer_t* w, int status)
{
   struct LwqqAsyncTimer_* io_ = w->data;
   if (io_->super.func)
      io_->super.func(w->data, io_->super.data);
}

static void(timer_start)(void* t, unsigned int ms)
{
   struct LwqqAsyncTimer_* t_ = (struct LwqqAsyncTimer_*)t;
   t_->h.data = t;
   uv_timer_init(loop, &t_->h);
   uv_timer_start(&t_->h, timer_cb_wrap, ms / 10, ms / 10);
}

static void(timer_stop)(void* t)
{
   struct LwqqAsyncTimer_* t_ = (struct LwqqAsyncTimer_*)t;
   uv_timer_stop(&t_->h);
}

static void(timer_again)(void* t)
{
   struct LwqqAsyncTimer_* t_ = (struct LwqqAsyncTimer_*)t;
   uv_timer_again(&t_->h);
}

static LwqqAsyncImpl impl_libuv = {
   .name = "libuv",
   .flags = USE_THREAD,
   .loop_create = loop_create,
   .loop_run = loop_run,
   .loop_stop = loop_stop,
   .loop_free = loop_free,

   .io_new = io_new,
   .io_free = io_free,
   .io_start = io_start,
   .io_stop = io_stop,

   .timer_new = timer_new,
   .timer_free = timer_free,
   .timer_start = timer_start,
   .timer_stop = timer_stop,
   .timer_again = timer_again,
};

