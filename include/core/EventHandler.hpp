#ifndef EVENT_HANDLER_HPP
#define EVENT_HANDLER_HPP

class EventHandler {
public:
    virtual ~EventHandler() {}
    virtual void handleRead() = 0;
    virtual void handleWrite() = 0;
    virtual void handleError() = 0;
    virtual int getFd() const = 0;
};

#endif
