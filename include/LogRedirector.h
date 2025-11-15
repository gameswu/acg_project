#pragma once

#include <iostream>
#include <sstream>
#include <functional>

namespace ACG {

class LogRedirector : public std::streambuf {
public:
    LogRedirector(std::ostream& stream, std::function<void(const std::string&)> callback)
        : m_stream(stream)
        , m_callback(callback)
        , m_oldBuf(stream.rdbuf(this))
    {
    }

    ~LogRedirector() {
        m_stream.rdbuf(m_oldBuf);
    }

protected:
    virtual int overflow(int c) override {
        if (c != EOF) {
            if (c == '\n') {
                if (m_callback && !m_buffer.str().empty()) {
                    m_callback(m_buffer.str());
                }
                m_buffer.str("");
                m_buffer.clear();
            } else {
                m_buffer << static_cast<char>(c);
            }
        }
        return c;
    }

    virtual std::streamsize xsputn(const char* s, std::streamsize n) override {
        for (std::streamsize i = 0; i < n; ++i) {
            overflow(s[i]);
        }
        return n;
    }

private:
    std::ostream& m_stream;
    std::function<void(const std::string&)> m_callback;
    std::streambuf* m_oldBuf;
    std::ostringstream m_buffer;
};

} // namespace ACG
