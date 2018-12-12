#include "websocket.h"
#include "util.h"

#include <utility>

Websocket::Websocket(boost::asio::io_context& ioc)
    : ws_(new websocket_t(ioc))
    , strand_(ws_->get_executor()) { }
Websocket::Websocket(boost::asio::io_context& ioc, boost::asio::ssl::context ssl_ctx)
    : wss_(new ssl_websocket_t(ioc, ssl_ctx))
    , strand_(wss_->get_executor()) { }
Websocket::Websocket(boost::asio::ip::tcp::socket socket)
    : ws_(new websocket_t(std::move(socket)))
    , strand_(ws_->get_executor()) { }

Websocket::~Websocket() {
    RTC_LOG(LS_INFO) << __FUNCTION__;
}

bool Websocket::isSSL() const { return wss_ != nullptr; }
Websocket::websocket_t& Websocket::nativeSocket() { return *ws_; }
Websocket::ssl_websocket_t& Websocket::nativeSecureSocket() { return *wss_; }

boost::asio::strand<boost::asio::io_context::executor_type>& Websocket::strand() { return strand_; }

void Websocket::startToRead(read_callback_t on_read) {
    boost::asio::post(
        strand_,
        std::bind(
            &Websocket::doRead,
            this,
            on_read));
}

void Websocket::doRead(read_callback_t on_read) {
    RTC_LOG(LS_INFO) << __FUNCTION__;

    if (isSSL()) {
        wss_->async_read(
            read_buffer_,
            boost::asio::bind_executor(
                strand_,
                std::bind(
                    &Websocket::onRead,
                    this,
                    on_read,
                    std::placeholders::_1,
                    std::placeholders::_2)));
    } else {
        ws_->async_read(
            read_buffer_,
            boost::asio::bind_executor(
                strand_,
                std::bind(
                    &Websocket::onRead,
                    this,
                    on_read,
                    std::placeholders::_1,
                    std::placeholders::_2)));
    }
}

void Websocket::onRead(read_callback_t on_read, boost::system::error_code ec, std::size_t bytes_transferred) {
    RTC_LOG(LS_INFO) << __FUNCTION__ << ": " << ec.message();

    // エラーだろうが何だろうが on_read コールバック関数は必ず呼ぶ

    const auto text = boost::beast::buffers_to_string(read_buffer_.data());
    read_buffer_.consume(read_buffer_.size());

    on_read(ec, bytes_transferred, std::move(text));

    if (ec == boost::asio::error::operation_aborted)
        return;

    if (ec)
        return MOMO_BOOST_ERROR(ec, "onRead");

    doRead(on_read);
}

void Websocket::sendText(std::string text) {
    RTC_LOG(LS_INFO) << __FUNCTION__;
    boost::asio::post(
        strand_,
        std::bind(
            &Websocket::doSendText,
            this,
            std::move(text)));
}

void Websocket::doSendText(std::string text) {
    RTC_LOG(LS_INFO) << __FUNCTION__ << ": " << text;

    bool empty = write_buffer_.empty();
    boost::beast::flat_buffer buffer;

    const auto n = boost::asio::buffer_copy(buffer.prepare(text.size()), boost::asio::buffer(text));
    buffer.commit(n);

    write_buffer_.push_back(std::move(buffer));

    if (empty) {
        doWrite();
    }
}
void Websocket::doWrite() {
    RTC_LOG(LS_INFO) << __FUNCTION__;

    auto& buffer = write_buffer_.front();
    if (isSSL()) {
        wss_->text(true);
        wss_->async_write(
            buffer.data(),
            boost::asio::bind_executor(
                strand_,
                std::bind(
                    &Websocket::onWrite,
                    this,
                    std::placeholders::_1,
                    std::placeholders::_2)));
    } else {
        ws_->text(true);
        ws_->async_write(
            buffer.data(),
            boost::asio::bind_executor(
                strand_,
                std::bind(
                    &Websocket::onWrite,
                    this,
                    std::placeholders::_1,
                    std::placeholders::_2)));
    }
}

void Websocket::onWrite(boost::system::error_code ec, std::size_t bytes_transferred)
{
    RTC_LOG(LS_INFO) << __FUNCTION__ << ": " << ec.message();

    // エラーだろうが何だろうが on_write_ コールバック関数は必ず呼ぶ
    // on_write(ec, bytes_transferred);

    if (ec == boost::asio::error::operation_aborted)
        return;

    if (ec)
        return MOMO_BOOST_ERROR(ec, "onWrite");

    write_buffer_.erase(write_buffer_.begin());

    if (!write_buffer_.empty())
    {
        doWrite();
    }
}