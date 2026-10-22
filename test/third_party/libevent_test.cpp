#include "globalmacros.h"

#ifdef DLOG_CLOUD_STORAGE_OS_WIN32
#include <winsock2.h>
#endif

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdlib>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <event2/buffer.h>
#include <event2/event.h>
#include <event2/http.h>


namespace
{
    using EventBasePtr = std::unique_ptr<event_base, decltype(&event_base_free)>;
    using EventPtr = std::unique_ptr<event, decltype(&event_free)>;
    using BufferPtr = std::unique_ptr<evbuffer, decltype(&evbuffer_free)>;
    using RequestPtr = std::unique_ptr<evhttp_request, decltype(&evhttp_request_free)>;
    using UriPtr = std::unique_ptr<evhttp_uri, decltype(&evhttp_uri_free)>;
    using CStringPtr = std::unique_ptr<char, decltype(&std::free)>;


    constexpr auto kNoDescriptor = static_cast<evutil_socket_t>(-1);


    struct CallbackResult
    {
        int m_calls = 0;
        short m_flags = 0;
        evutil_socket_t m_descriptor = kNoDescriptor;
    };

    void recordEvent(evutil_socket_t descriptor, short flags, void *arg)
    {
        auto &result = *static_cast<CallbackResult *>(arg);
        ++result.m_calls;
        result.m_flags = flags;
        result.m_descriptor = descriptor;
    }

    void expectBufferContent(evbuffer *buffer, const std::string &expected)
    {
        ASSERT_EQ(evbuffer_get_length(buffer), expected.size());
        std::string actual(expected.size(), '\0');
        EXPECT_EQ(evbuffer_copyout(buffer, actual.data(), actual.size()), static_cast<ev_ssize_t>(actual.size()));
        EXPECT_EQ(actual, expected);
    }

    class LibeventTests : public ::testing::Test
    {

    protected:

        static void SetUpTestSuite()
        {
#ifdef DLOG_CLOUD_STORAGE_OS_WIN32
            WSADATA data{};
            ASSERT_EQ(WSAStartup(MAKEWORD(2, 2), &data), 0);
            s_winsockInitialized = true;
#endif
        }

        static void TearDownTestSuite()
        {
#ifdef DLOG_CLOUD_STORAGE_OS_WIN32
            if (s_winsockInitialized)
            {
                EXPECT_EQ(WSACleanup(), 0);
                s_winsockInitialized = false;
            }
#endif
        }

        void SetUp() override
        {
            m_base.reset(event_base_new());
            ASSERT_NE(m_base, nullptr);
        }

        EventPtr makeEvent(CallbackResult &result, short flags = 0)
        {
            return EventPtr(event_new(m_base.get(), kNoDescriptor, flags, recordEvent, &result), event_free);
        }

        EventBasePtr m_base{nullptr, event_base_free};
        CallbackResult m_result;

#ifdef DLOG_CLOUD_STORAGE_OS_WIN32
        inline static bool s_winsockInitialized = false;
#endif
    };
}


TEST_F(LibeventTests, ReportsMatchingVersionAndSupportedBackend)
{
    ASSERT_NE(event_get_version(), nullptr);
    EXPECT_NE(*event_get_version(), '\0');
    EXPECT_EQ(event_get_version_number(), LIBEVENT_VERSION_NUMBER);

    const auto *backend = event_base_get_method(m_base.get());
    ASSERT_NE(backend, nullptr);
    const auto *methods = event_get_supported_methods();
    ASSERT_NE(methods, nullptr);
    std::vector<std::string> supported;
    for (auto method = methods; *method; ++method) supported.emplace_back(*method);
    EXPECT_NE(std::find(supported.begin(), supported.end(), backend), supported.end());
}

TEST_F(LibeventTests, EmptyLoopReturnsWithoutBlocking)
{
    EXPECT_EQ(event_base_loop(m_base.get(), EVLOOP_NONBLOCK), 1);
}

TEST_F(LibeventTests, ActivationDeliversFlagsAndCallbackContext)
{
    auto event = makeEvent(m_result);
    ASSERT_NE(event, nullptr);
    EXPECT_EQ(event_get_base(event.get()), m_base.get());
    EXPECT_EQ(event_get_callback_arg(event.get()), &m_result);

    event_active(event.get(), EV_READ, 1);
    ASSERT_GE(event_base_loop(m_base.get(), EVLOOP_NONBLOCK), 0);
    EXPECT_EQ(m_result.m_calls, 1);
    EXPECT_EQ(m_result.m_flags, EV_READ);
    EXPECT_EQ(m_result.m_descriptor, kNoDescriptor);
}

TEST_F(LibeventTests, RepeatedActivationCoalescesCallbackFlags)
{
    auto event = makeEvent(m_result);
    ASSERT_NE(event, nullptr);
    event_active(event.get(), EV_READ, 1);
    event_active(event.get(), EV_WRITE, 1);

    ASSERT_GE(event_base_loop(m_base.get(), EVLOOP_NONBLOCK), 0);
    EXPECT_EQ(m_result.m_calls, 1);
    EXPECT_EQ(m_result.m_flags, EV_READ | EV_WRITE);
}

TEST_F(LibeventTests, OneShotTimerFiresOnceAndCanBeRearmed)
{
    auto timer = makeEvent(m_result);
    ASSERT_NE(timer, nullptr);
    const timeval immediately{0, 0};

    for (int expectedCalls = 1; expectedCalls <= 2; ++expectedCalls)
    {
        ASSERT_EQ(evtimer_add(timer.get(), &immediately), 0);
        EXPECT_NE(evtimer_pending(timer.get(), nullptr), 0);
        ASSERT_GE(event_base_loop(m_base.get(), EVLOOP_NONBLOCK), 0);
        EXPECT_EQ(m_result.m_calls, expectedCalls);
        EXPECT_EQ(m_result.m_flags, EV_TIMEOUT);
        EXPECT_EQ(evtimer_pending(timer.get(), nullptr), 0);

        ASSERT_GE(event_base_loop(m_base.get(), EVLOOP_NONBLOCK), 0);
        EXPECT_EQ(m_result.m_calls, expectedCalls);
    }
}

TEST_F(LibeventTests, ReschedulingTimerReplacesExistingTimeout)
{
    auto timer = makeEvent(m_result);
    ASSERT_NE(timer, nullptr);
    const timeval later{30, 0};
    const timeval immediately{0, 0};
    ASSERT_EQ(event_add(timer.get(), &later), 0);
    ASSERT_EQ(event_add(timer.get(), &immediately), 0);

    ASSERT_GE(event_base_loop(m_base.get(), EVLOOP_NONBLOCK), 0);
    EXPECT_EQ(m_result.m_calls, 1);
    EXPECT_EQ(event_pending(timer.get(), EV_TIMEOUT, nullptr), 0);
}

TEST_F(LibeventTests, DeletingPendingTimerCancelsCallback)
{
    auto timer = makeEvent(m_result);
    ASSERT_NE(timer, nullptr);
    const timeval later{30, 0};
    ASSERT_EQ(event_add(timer.get(), &later), 0);
    EXPECT_NE(event_pending(timer.get(), EV_TIMEOUT, nullptr), 0);
    ASSERT_EQ(event_del(timer.get()), 0);

    EXPECT_EQ(event_pending(timer.get(), EV_TIMEOUT, nullptr), 0);
    EXPECT_EQ(event_base_loop(m_base.get(), EVLOOP_NONBLOCK), 1);
    EXPECT_EQ(m_result.m_calls, 0);
}

TEST_F(LibeventTests, FreeingPendingTimerCancelsCallback)
{
    auto timer = makeEvent(m_result);
    ASSERT_NE(timer, nullptr);
    const timeval immediately{0, 0};
    ASSERT_EQ(event_add(timer.get(), &immediately), 0);
    timer.reset();

    EXPECT_EQ(event_base_loop(m_base.get(), EVLOOP_NONBLOCK), 1);
    EXPECT_EQ(m_result.m_calls, 0);
}

TEST_F(LibeventTests, FreeingActiveEventCancelsCallback)
{
    auto event = makeEvent(m_result);
    ASSERT_NE(event, nullptr);
    event_active(event.get(), EV_READ, 1);
    event.reset();

    EXPECT_EQ(event_base_loop(m_base.get(), EVLOOP_NONBLOCK), 1);
    EXPECT_EQ(m_result.m_calls, 0);
}

TEST_F(LibeventTests, HigherPriorityActiveEventRunsFirst)
{
    ASSERT_EQ(event_base_priority_init(m_base.get(), 2), 0);
    std::vector<int> order;
    order.reserve(2);
    struct Context
    {
        std::vector<int> &m_order;
        int m_id;
    };
    Context lowContext{order, 1};
    Context highContext{order, 0};
    const auto callback = [](evutil_socket_t, short, void *arg)
    {
        auto &context = *static_cast<Context *>(arg);
        context.m_order.push_back(context.m_id);
    };
    EventPtr low(event_new(m_base.get(), kNoDescriptor, 0, callback, &lowContext), event_free);
    EventPtr high(event_new(m_base.get(), kNoDescriptor, 0, callback, &highContext), event_free);
    ASSERT_NE(low, nullptr);
    ASSERT_NE(high, nullptr);
    ASSERT_EQ(event_priority_set(low.get(), 1), 0);
    ASSERT_EQ(event_priority_set(high.get(), 0), 0);
    event_active(low.get(), EV_TIMEOUT, 1);
    event_active(high.get(), EV_TIMEOUT, 1);

    ASSERT_GE(event_base_dispatch(m_base.get()), 0);
    EXPECT_EQ(order, (std::vector<int>{0, 1}));
}

TEST_F(LibeventTests, LoopBreakDefersRemainingCallbacksUntilNextLoop)
{
    ASSERT_EQ(event_base_priority_init(m_base.get(), 2), 0);
    EventPtr stopper(event_new(
                         m_base.get(), kNoDescriptor, 0,
                         [](evutil_socket_t, short, void *arg)
                         {
                             event_base_loopbreak(static_cast<event_base *>(arg));
                         },
                         m_base.get()),
                     event_free);
    auto remaining = makeEvent(m_result);
    ASSERT_NE(stopper, nullptr);
    ASSERT_NE(remaining, nullptr);
    ASSERT_EQ(event_priority_set(stopper.get(), 0), 0);
    ASSERT_EQ(event_priority_set(remaining.get(), 1), 0);
    event_active(remaining.get(), EV_TIMEOUT, 1);
    event_active(stopper.get(), EV_TIMEOUT, 1);

    ASSERT_EQ(event_base_dispatch(m_base.get()), 0);
    EXPECT_NE(event_base_got_break(m_base.get()), 0);
    EXPECT_EQ(m_result.m_calls, 0);

    ASSERT_GE(event_base_loop(m_base.get(), EVLOOP_NONBLOCK), 0);
    EXPECT_EQ(m_result.m_calls, 1);
    EXPECT_EQ(event_base_got_break(m_base.get()), 0);
}

TEST_F(LibeventTests, LoopExitStopsWhileAnotherTimerRemainsPending)
{
    auto timer = makeEvent(m_result);
    ASSERT_NE(timer, nullptr);
    const timeval later{30, 0};
    ASSERT_EQ(event_add(timer.get(), &later), 0);
    ASSERT_EQ(event_base_loopexit(m_base.get(), nullptr), 0);

    ASSERT_EQ(event_base_dispatch(m_base.get()), 0);
    EXPECT_NE(event_base_got_exit(m_base.get()), 0);
    EXPECT_NE(event_pending(timer.get(), EV_TIMEOUT, nullptr), 0);
    EXPECT_EQ(m_result.m_calls, 0);
}

TEST_F(LibeventTests, PersistentTimerRepeatsUntilDeletedInsideCallback)
{
    struct Context
    {
        event_base *m_base;
        event *m_timer = nullptr;
        int m_calls = 0;
        int m_deleteResult = -1;
    } context{m_base.get()};
    EventPtr timer(event_new(
                       m_base.get(), kNoDescriptor, EV_PERSIST,
                       [](evutil_socket_t, short, void *arg)
                       {
                           auto &context = *static_cast<Context *>(arg);
                           if (++context.m_calls == 3)
                           {
                               context.m_deleteResult = event_del(context.m_timer);
                               event_base_loopbreak(context.m_base);
                           }
                       },
                       &context),
                   event_free);
    ASSERT_NE(timer, nullptr);
    context.m_timer = timer.get();
    const timeval interval{0, 1000};
    ASSERT_EQ(event_add(timer.get(), &interval), 0);

    // 只断言触发次数，不断言耗时；退出期限防止失败时无限等待。
    const timeval deadline{2, 0};
    ASSERT_EQ(event_base_loopexit(m_base.get(), &deadline), 0);
    ASSERT_EQ(event_base_dispatch(m_base.get()), 0);
    EXPECT_EQ(context.m_calls, 3);
    EXPECT_EQ(context.m_deleteResult, 0);
    EXPECT_EQ(event_pending(timer.get(), EV_TIMEOUT, nullptr), 0);
    EXPECT_NE(event_base_got_break(m_base.get()), 0);
}

TEST_F(LibeventTests, BufferStartsEmptyAndAcceptsEmptyInput)
{
    BufferPtr buffer(evbuffer_new(), evbuffer_free);
    ASSERT_NE(buffer, nullptr);
    EXPECT_EQ(evbuffer_get_length(buffer.get()), 0U);
    EXPECT_EQ(evbuffer_add(buffer.get(), "", 0), 0);
    EXPECT_EQ(evbuffer_get_length(buffer.get()), 0U);
}

TEST_F(LibeventTests, BufferAppendPreservesEmbeddedNulsAndHighBytes)
{
    BufferPtr buffer(evbuffer_new(), evbuffer_free);
    ASSERT_NE(buffer, nullptr);
    const std::string first("a\0b", 3);
    const std::string second("\x7f\x80\xff", 3);
    ASSERT_EQ(evbuffer_add(buffer.get(), first.data(), first.size()), 0);
    ASSERT_EQ(evbuffer_add(buffer.get(), second.data(), second.size()), 0);

    expectBufferContent(buffer.get(), first + second);
}

TEST_F(LibeventTests, BufferCopyoutDoesNotConsumeData)
{
    BufferPtr buffer(evbuffer_new(), evbuffer_free);
    ASSERT_NE(buffer, nullptr);
    const std::string payload("a\0b\xff", 4);
    ASSERT_EQ(evbuffer_add(buffer.get(), payload.data(), payload.size()), 0);

    expectBufferContent(buffer.get(), payload);
    expectBufferContent(buffer.get(), payload);
}

TEST_F(LibeventTests, BufferRemoveConsumesOnlyAvailableBytes)
{
    BufferPtr buffer(evbuffer_new(), evbuffer_free);
    ASSERT_NE(buffer, nullptr);
    ASSERT_EQ(evbuffer_add(buffer.get(), "abcdef", 6), 0);
    std::string output(3, '\0');
    ASSERT_EQ(evbuffer_remove(buffer.get(), output.data(), output.size()), 3);
    EXPECT_EQ(output, "abc");
    expectBufferContent(buffer.get(), "def");

    output.assign(10, '\0');
    ASSERT_EQ(evbuffer_remove(buffer.get(), output.data(), output.size()), 3);
    EXPECT_EQ(output.substr(0, 3), "def");
    EXPECT_EQ(evbuffer_get_length(buffer.get()), 0U);
}

TEST_F(LibeventTests, BufferDrainClearsDataAndAllowsReuse)
{
    BufferPtr buffer(evbuffer_new(), evbuffer_free);
    ASSERT_NE(buffer, nullptr);
    ASSERT_EQ(evbuffer_add(buffer.get(), "abcdef", 6), 0);
    ASSERT_EQ(evbuffer_drain(buffer.get(), 2), 0);
    expectBufferContent(buffer.get(), "cdef");
    ASSERT_EQ(evbuffer_drain(buffer.get(), 100), 0);
    EXPECT_EQ(evbuffer_get_length(buffer.get()), 0U);
    ASSERT_EQ(evbuffer_add(buffer.get(), "new", 3), 0);
    expectBufferContent(buffer.get(), "new");
}

TEST_F(LibeventTests, BufferMoveAppendsContentAndEmptiesSource)
{
    BufferPtr source(evbuffer_new(), evbuffer_free);
    BufferPtr destination(evbuffer_new(), evbuffer_free);
    ASSERT_NE(source, nullptr);
    ASSERT_NE(destination, nullptr);
    const std::string payload("a\0b", 3);
    ASSERT_EQ(evbuffer_add(source.get(), payload.data(), payload.size()), 0);
    ASSERT_EQ(evbuffer_add(destination.get(), "prefix:", 7), 0);
    ASSERT_EQ(evbuffer_add_buffer(destination.get(), source.get()), 0);

    EXPECT_EQ(evbuffer_get_length(source.get()), 0U);
    expectBufferContent(destination.get(), "prefix:" + payload);
}

TEST_F(LibeventTests, BufferReadlnWaitsForCompleteCrlf)
{
    BufferPtr buffer(evbuffer_new(), evbuffer_free);
    ASSERT_NE(buffer, nullptr);
    ASSERT_EQ(evbuffer_add(buffer.get(), "first\r", 6), 0);
    size_t length = 0;
    CStringPtr incomplete(evbuffer_readln(buffer.get(), &length, EVBUFFER_EOL_CRLF_STRICT), std::free);
    EXPECT_EQ(incomplete, nullptr);
    EXPECT_EQ(evbuffer_get_length(buffer.get()), 6U);

    constexpr std::string_view remainder = "\nsecond\r\nunfinished";
    ASSERT_EQ(evbuffer_add(buffer.get(), remainder.data(), remainder.size()), 0);
    CStringPtr first(evbuffer_readln(buffer.get(), &length, EVBUFFER_EOL_CRLF_STRICT), std::free);
    ASSERT_NE(first, nullptr);
    EXPECT_EQ(std::string(first.get(), length), "first");
    CStringPtr second(evbuffer_readln(buffer.get(), &length, EVBUFFER_EOL_CRLF_STRICT), std::free);
    ASSERT_NE(second, nullptr);
    EXPECT_EQ(std::string(second.get(), length), "second");
    expectBufferContent(buffer.get(), "unfinished");
}

TEST_F(LibeventTests, UriParserSeparatesEncodedPathAndQuery)
{
    UriPtr uri(evhttp_uri_parse("http://127.0.0.1:8082/download/a%20b.txt?type=uncompressed#section"), evhttp_uri_free);
    ASSERT_NE(uri, nullptr);
    EXPECT_STREQ(evhttp_uri_get_scheme(uri.get()), "http");
    EXPECT_STREQ(evhttp_uri_get_host(uri.get()), "127.0.0.1");
    EXPECT_EQ(evhttp_uri_get_port(uri.get()), 8082);
    EXPECT_STREQ(evhttp_uri_get_path(uri.get()), "/download/a%20b.txt");
    EXPECT_STREQ(evhttp_uri_get_query(uri.get()), "type=uncompressed");
    EXPECT_STREQ(evhttp_uri_get_fragment(uri.get()), "section");
}

TEST_F(LibeventTests, UriDecoderControlsWhetherPlusBecomesSpace)
{
    constexpr auto encoded = "/download/a+b%20%E6%96%87.txt";
    size_t length = 0;
    CStringPtr path(evhttp_uridecode(encoded, 0, &length), std::free);
    ASSERT_NE(path, nullptr);
    EXPECT_EQ(std::string(path.get(), length), u8"/download/a+b 文.txt");

    CStringPtr form(evhttp_uridecode(encoded, 1, &length), std::free);
    ASSERT_NE(form, nullptr);
    EXPECT_EQ(std::string(form.get(), length), u8"/download/a b 文.txt");
}

TEST_F(LibeventTests, QueryParserDecodesEscapesAndPlusAsSpace)
{
    RequestPtr request(evhttp_request_new(nullptr, nullptr), evhttp_request_free);
    ASSERT_NE(request, nullptr);
    auto *parameters = evhttp_request_get_input_headers(request.get());
    ASSERT_NE(parameters, nullptr);
    ASSERT_EQ(evhttp_parse_query_str("name=a%2Bb+file.txt&type=uncompressed&empty=", parameters), 0);

    EXPECT_STREQ(evhttp_find_header(parameters, "name"), "a+b file.txt");
    EXPECT_STREQ(evhttp_find_header(parameters, "type"), "uncompressed");
    EXPECT_STREQ(evhttp_find_header(parameters, "empty"), "");
    EXPECT_EQ(evhttp_find_header(parameters, "missing"), nullptr);
}

TEST_F(LibeventTests, HttpHeadersAreCaseInsensitiveAndCanBeRemoved)
{
    RequestPtr request(evhttp_request_new(nullptr, nullptr), evhttp_request_free);
    ASSERT_NE(request, nullptr);
    auto *headers = evhttp_request_get_input_headers(request.get());
    ASSERT_NE(headers, nullptr);
    ASSERT_EQ(evhttp_add_header(headers, "StorageType", "compressed"), 0);
    ASSERT_EQ(evhttp_add_header(headers, "FileName", "YS50eHQ="), 0);
    EXPECT_STREQ(evhttp_find_header(headers, "storagetype"), "compressed");
    EXPECT_STREQ(evhttp_find_header(headers, "FILENAME"), "YS50eHQ=");

    ASSERT_EQ(evhttp_remove_header(headers, "STORAGETYPE"), 0);
    EXPECT_EQ(evhttp_find_header(headers, "StorageType"), nullptr);
    EXPECT_EQ(evhttp_remove_header(headers, "missing"), -1);
    EXPECT_STREQ(evhttp_find_header(headers, "FileName"), "YS50eHQ=");
}

TEST_F(LibeventTests, UriParserRejectsMalformedHost)
{
    UriPtr uri(evhttp_uri_parse("http://[::1"), evhttp_uri_free);
    EXPECT_EQ(uri, nullptr);
}

TEST_F(LibeventTests, HttpRequestHasIndependentInputAndOutputBuffers)
{
    RequestPtr request(evhttp_request_new(nullptr, nullptr), evhttp_request_free);
    ASSERT_NE(request, nullptr);
    auto *input = evhttp_request_get_input_buffer(request.get());
    auto *output = evhttp_request_get_output_buffer(request.get());
    ASSERT_NE(input, nullptr);
    ASSERT_NE(output, nullptr);
    EXPECT_NE(input, output);
    const std::string payload("in\0data", 7);
    ASSERT_EQ(evbuffer_add(input, payload.data(), payload.size()), 0);
    ASSERT_EQ(evbuffer_add(output, "out", 3), 0);

    expectBufferContent(input, payload);
    expectBufferContent(output, "out");
}
