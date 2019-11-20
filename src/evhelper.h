/**
 * Copyright - See the COPYRIGHT that is included with this distribution.
 * pvxs is distributed subject to a Software License Agreement found
 * in file LICENSE that is included with this distribution.
 */

#ifndef EVHELPER_H
#define EVHELPER_H

#include <sstream>
#include <functional>
#include <memory>
#include <string>

#include <event2/event.h>
#include <event2/buffer.h>
#include <event2/listener.h>
#include <event2/bufferevent.h>

#include <pvxs/version.h>
#include <utilpvt.h>

#include "pvaproto.h"

// hooks for std::unique_ptr
namespace std {
template<>
struct default_delete<event> {
    inline void operator()(event* ev) { event_free(ev); }
};
template<>
struct default_delete<evconnlistener> {
    inline void operator()(evconnlistener* ev) { evconnlistener_free(ev); }
};
template<>
struct default_delete<bufferevent> {
    inline void operator()(bufferevent* ev) { bufferevent_free(ev); }
};
template<>
struct default_delete<evbuffer> {
    inline void operator()(evbuffer* ev) { evbuffer_free(ev); }
};
}

namespace pvxs {namespace impl {

//! unique_ptr which is never constructed with NULL
template<typename T>
struct owned_ptr : public std::unique_ptr<T>
{
    constexpr owned_ptr() {}
    explicit owned_ptr(T* ptr) : std::unique_ptr<T>(ptr) {
        if(!*this)
            throw std::bad_alloc();
    }
};

struct PVXS_API evbase {
    explicit evbase(const std::string& name, unsigned prio=0);
    ~evbase();
    void start();

    void sync();

    // queue request to execute in event loop.  return immediately.
    void dispatch(std::function<void()>&& fn);
    // queue request to execute in event loop.  return after executed
    void call(std::function<void()>&& fn);

    void assertInLoop();
    bool inLoop();

private:
    struct Pvt;
    std::unique_ptr<Pvt> pvt;
public:
    event_base* const base;
};

typedef owned_ptr<event> evevent;
typedef owned_ptr<evconnlistener> evlisten;
typedef owned_ptr<bufferevent> evbufferevent;
typedef owned_ptr<evbuffer> evbuf;

template <typename Buf>
void to_wire(Buf& buf, const SockAddr& val)
{
    if(!buf.ensure(16)) {
        buf.fault();
        return;

    } else if(val.family()==AF_INET) {
        for(unsigned i=0; i<10; i++)
            buf[i]=0;
        buf[10] = buf[11] = 0xff;

        memcpy(buf.save()+12, &val->in.sin_addr.s_addr, 4);

    } else if(val.family()==AF_INET6) {
        static_assert (sizeof(val->in6.sin6_addr)==16, "");
        memcpy(buf.save(), &val->in6.sin6_addr, 16);
    }
    buf._skip(16);
}

template <typename Buf>
void from_wire(Buf &buf, SockAddr& val)
{
    if(!buf.ensure(16)) {
        buf.fault();
        return;
    }

    // win32 lacks IN6_IS_ADDR_V4MAPPED()
    bool ismapped = true;
    for(unsigned i=0u; i<10; i++)
        ismapped &= buf[i]==0;
    ismapped &= buf[10]==0xff;
    ismapped &= buf[11]==0xff;

    if(ismapped) {
        val->in = {};
        val->in.sin_family = AF_INET;
        memcpy(&val->in.sin_addr.s_addr, buf.save()+12, 4);

    } else {
        val->in6 = {};
        val->in6.sin6_family = AF_INET6;

        static_assert (sizeof(val->in6.sin6_addr)==16, "");
        memcpy(&val->in6.sin6_addr, buf.save(), 16);
    }
    buf._skip(16);
}

struct PVXS_API evsocket
{
    evutil_socket_t sock;

    // default construct an invalid socket
    constexpr evsocket() noexcept :sock(-1) {}

    // construct from a valid (not -1) socket
    explicit evsocket(evutil_socket_t sock);

    // create a new socket
    evsocket(int, int, int);

    // movable
    evsocket(evsocket&& o) noexcept;
    evsocket& operator=(evsocket&&) noexcept;

    // not copyable
    evsocket(const evsocket&) = delete;
    evsocket& operator=(const evsocket&) = delete;

    ~evsocket();

    // test validity
    inline operator bool() const { return sock!=-1; }

    void bind(SockAddr& addr) const;
    //! join mcast group.  Receive mcasts send to this group which arrive on the given interface
    //! @see IP_ADD_MEMBERSHIP
    void mcast_join(const SockAddr& grp, const SockAddr& iface) const;
    //! Set time-to-live out mcasts sent from this socket
    //! @see IP_MULTICAST_TTL
    void mcast_ttl(unsigned ttl) const;
    //! Whether mcasts sent from this socket should be received to local listeners
    //! @see IP_MULTICAST_LOOP
    void mcast_loop(bool loop) const;
    //! Selects interface to use when sending mcasts
    //! @see IP_MULTICAST_IF
    void mcast_iface(const SockAddr& iface) const;
};

}} // namespace pvxs::impl

#endif /* EVHELPER_H */
