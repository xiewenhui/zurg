// Copyright (c) 2014 Baidu.com, Inc. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Author: qinzuoyan01@baidu.com (Qin Zuoyan)

#include <sofa/pbrpc/rpc_server_impl.h>
#include <sofa/pbrpc/rpc_controller_impl.h>
#include <sofa/pbrpc/tran_buf_pool.h>
#include <sofa/pbrpc/flow_controller.h>
#include <sofa/pbrpc/builtin_service_impl.h>
#include <sofa/pbrpc/closure.h>
#include <sofa/pbrpc/ptime.h>
#include <sofa/pbrpc/compressed_stream.h>

namespace sofa {
namespace pbrpc {

RpcServerImpl::RpcServerImpl(const RpcServerOptions& options,
        RpcServer::EventHandler* handler)
    : _options(options)
    , _event_handler(handler)
    , _is_running(false)
    , _epoch_time(ptime_now())
    , _ticks_per_second(time_duration_seconds(1).ticks())
    , _last_maintain_ticks(0)
    , _last_restart_listen_ticks(0)
    , _last_switch_stat_slot_ticks(0)
    , _last_print_connection_ticks(0)
    , _live_stream_count(0)
{
    _service_pool.reset(new ServicePool());

    _slice_count = std::max(1, 1000 / MAINTAIN_INTERVAL_IN_MS);
    _slice_quota_in = _options.max_throughput_in == -1 ?
        -1 : std::max(0L, _options.max_throughput_in * 1024L * 1024L) / _slice_count;
    _slice_quota_out = _options.max_throughput_out == -1 ?
        -1 : std::max(0L, _options.max_throughput_out * 1024L * 1024L) / _slice_count;
    _max_pending_buffer_size =
        std::max(0L, _options.max_pending_buffer_size * 1024L * 1024L);
    _keep_alive_ticks = _options.keep_alive_time == -1 ?
        -1 : std::max(1, _options.keep_alive_time) * _ticks_per_second;
    _restart_listen_interval_ticks = _ticks_per_second * 3;
    _switch_stat_slot_interval_ticks = _ticks_per_second * STAT_SLOT_SECONDS;
    _print_connection_interval_ticks = _ticks_per_second * 60;

#if defined( LOG )
    LOG(INFO) << "RpcServerImpl(): quota_in="
              << (_slice_quota_in == -1 ? -1 : _slice_quota_in * _slice_count / (1024L * 1024L))
              << "MB/s, quota_out="
              << (_slice_quota_out == -1 ? -1 : _slice_quota_out * _slice_count / (1024L * 1024L))
              << "MB/s, max_pending_buffer_size="
              << (_max_pending_buffer_size / (1024L * 1024L))
              << "MB, keep_alive_time="
              << (_keep_alive_ticks == -1 ? -1 : _keep_alive_ticks / _ticks_per_second)
              << "seconds";
#else
    SLOG(INFO, "RpcServerImpl(): quota_in=%lldMB/s, quota_out=%lldMB/s"
            ", max_pending_buffer_size=%lldMB, keep_alive_time=%lldseconds",
            _slice_quota_in == -1 ? -1 : _slice_quota_in * _slice_count / (1024L * 1024L),
            _slice_quota_out == -1 ? -1 : _slice_quota_out * _slice_count / (1024L * 1024L),
            _max_pending_buffer_size / (1024L * 1024L),
            _keep_alive_ticks == -1 ? -1 : _keep_alive_ticks / _ticks_per_second);
#endif
}

RpcServerImpl::~RpcServerImpl()
{
    SOFA_PBRPC_FUNCTION_TRACE;
    Stop();
    _service_pool.reset();
    if (_event_handler) delete _event_handler;
}

bool RpcServerImpl::Start(const std::string& server_address)
{
    ScopedLocker<MutexLock> _(_start_stop_lock);
    if (_is_running) return true;

    _flow_controller.reset(new FlowController(
                _slice_quota_in == -1, _slice_quota_in,
                _slice_quota_out == -1, _slice_quota_out));

    _maintain_thread_group.reset(new ThreadGroupImpl(
                1, "sofa_pbrpc_server_maintain_thread_group"));
    if (!_maintain_thread_group->start())
    {
#if defined( LOG )
        LOG(ERROR) << "Start(): start maintain thread group failed";
#else
        SLOG(ERROR, "Start(): start maintain thread group failed");
#endif
        _maintain_thread_group.reset();
        _flow_controller.reset();
    }

    _work_thread_group.reset(new ThreadGroupImpl(
                _options.work_thread_num, "sofa_pbrpc_server_work_thread_group"));
    _work_thread_group->set_init_func(_options.work_thread_init_func);
    _work_thread_group->set_dest_func(_options.work_thread_dest_func);
    if (!_work_thread_group->start())
    {
#if defined( LOG )
        LOG(ERROR) << "Start(): start work thread group failed";
#else
        SLOG(ERROR, "Start(): start work thread group failed");
#endif
        _work_thread_group.reset();
        _maintain_thread_group.reset();
        _flow_controller.reset();
        return false;
    }

    _server_address = server_address;
    if (!ResolveAddress(_work_thread_group->io_service(), _server_address, &_listen_endpoint))
    {
#if defined( LOG )
        LOG(ERROR) << "Start(): resolve server address failed: " << _server_address;
#else
        SLOG(ERROR, "Start(): resolve server address failed: %s", _server_address.c_str());
#endif
        _work_thread_group.reset();
        _maintain_thread_group.reset();
        _flow_controller.reset();
        return false;
    }

    _listener.reset(new RpcListener(_work_thread_group->io_service(), _listen_endpoint));
    _listener->set_create_callback(boost::bind(
                &RpcServerImpl::OnCreated, shared_from_this(), _1));
    _listener->set_accept_callback(boost::bind(
                &RpcServerImpl::OnAccepted, shared_from_this(), _1));
    _listener->set_accept_fail_callback(boost::bind(
                &RpcServerImpl::OnAcceptFailed, shared_from_this(), _1, _2));
    if (!_listener->start_listen())
    {
#if defined( LOG )
        LOG(ERROR) << "Start(): listen failed: " << _server_address;
#else
        SLOG(ERROR, "Start(): listen failed: %s", _server_address.c_str());
#endif
        _listener.reset();
        _work_thread_group.reset();
        _maintain_thread_group.reset();
        _flow_controller.reset();
        return false;
    }
#if defined( LOG )
    LOG(INFO) << "Start(): listen succeed: " << _server_address
              << " [" << RpcEndpointToString(_listen_endpoint) << "]";
#else
    SLOG(INFO, "Start(): listen succeed: %s [%s]",
            _server_address.c_str(), RpcEndpointToString(_listen_endpoint).c_str());
#endif

    _timer_worker.reset(new TimerWorker(_maintain_thread_group->io_service()));
    _timer_worker->set_time_duration(time_duration_milliseconds(MAINTAIN_INTERVAL_IN_MS));
    _timer_worker->set_work_routine(boost::bind(
                &RpcServerImpl::TimerMaintain, shared_from_this(), _1));
    _timer_worker->start();

    if (!_options.disable_builtin_services) {
        _service_pool->RegisterService(new sofa::pbrpc::builtin::BuiltinServiceImpl(
                    shared_from_this(), _service_pool, _options.disable_list_service));
    }

    _is_running = true;
#if defined( LOG )
    LOG(INFO) << "Start(): rpc server started";
#else
    SLOG(INFO, "Start(): rpc server started");
#endif
    return true;
}

void RpcServerImpl::Stop()
{
    ScopedLocker<MutexLock> _(_start_stop_lock);
    if (!_is_running) return;
    _is_running = false;

    _timer_worker->stop();
    _listener->close();
    StopStreams();
    _work_thread_group->stop();

    _timer_worker.reset();
    _listener.reset();
    ClearStreams();
    _maintain_thread_group->stop();

    _work_thread_group.reset();
    _maintain_thread_group.reset();
    _flow_controller.reset();

#if defined( LOG )
    LOG(INFO) << "Stop(): rpc server stopped";
#else
    SLOG(INFO, "Stop(): rpc server stopped");
#endif
}

RpcServerOptions RpcServerImpl::GetOptions()
{
    return _options;
}

void RpcServerImpl::ResetOptions(const RpcServerOptions& options)
{
    int64 old_slice_quota_in = _slice_quota_in;
    int64 old_slice_quota_out = _slice_quota_out;
    int64 old_max_pending_buffer_size = _max_pending_buffer_size;
    int64 old_keep_alive_ticks = _keep_alive_ticks;

    _options.max_throughput_in = options.max_throughput_in;
    _options.max_throughput_out = options.max_throughput_out;
    _options.max_pending_buffer_size = options.max_pending_buffer_size;
    _options.keep_alive_time = options.keep_alive_time;

    _slice_quota_in = _options.max_throughput_in == -1 ?
        -1 : std::max(0L, _options.max_throughput_in * 1024L * 1024L) / _slice_count;
    _slice_quota_out = _options.max_throughput_out == -1 ?
        -1 : std::max(0L, _options.max_throughput_out * 1024L * 1024L) / _slice_count;
    _max_pending_buffer_size = 
        std::max(0L, _options.max_pending_buffer_size * 1024L * 1024L);
    _keep_alive_ticks = _options.keep_alive_time == -1 ?
        -1 : std::max(1, _options.keep_alive_time) * _ticks_per_second;

    if (_max_pending_buffer_size != old_max_pending_buffer_size)
    {
        ScopedLocker<FastLock> _(_stream_list_lock);
        for (StreamList::iterator it = _stream_list.begin(); it != _stream_list.end(); ++it)
        {
            (*it)->set_max_pending_buffer_size(_max_pending_buffer_size);
        }
    }

    if (_slice_quota_in != old_slice_quota_in)
    {
        _flow_controller->reset_read_quota(_slice_quota_in == -1, _slice_quota_in);
    }

    if (_slice_quota_out != old_slice_quota_out)
    {
        _flow_controller->reset_write_quota(_slice_quota_out == -1, _slice_quota_out);
    }

#if defined( LOG )
    LOG(INFO) << "ResetOptions(): quota_in="
              << (_slice_quota_in == -1 ? -1 : _slice_quota_in * _slice_count / (1024L * 1024L))
              << "MB/s(old "
              << (old_slice_quota_in == -1 ? -1 : old_slice_quota_in * _slice_count / (1024L * 1024L))
              << "MB/s), quota_out="
              << (_slice_quota_out == -1 ? -1 : _slice_quota_out * _slice_count / (1024L * 1024L))
              << "MB/s(old "
              << (old_slice_quota_out == -1 ? -1 : old_slice_quota_out * _slice_count / (1024L * 1024L))
              << "MB/s), max_pending_buffer_size="
              << (_max_pending_buffer_size / (1024L * 1024L))
              << "MB(old "
              << (old_max_pending_buffer_size / (1024L * 1024L))
              << "MB), keep_alive_time="
              << (_keep_alive_ticks == -1 ? -1 : _keep_alive_ticks / _ticks_per_second)
              << "seconds(old "
              << (old_keep_alive_ticks == -1 ? -1 : old_keep_alive_ticks / _ticks_per_second)
              << "seconds)";
#else
    SLOG(INFO, "ResetOptions(): quota_in=%lldMB/s(old %lldMB/s)"
            ", quota_out=%lldMB/s(old %lldMB/s)"
            ", max_pending_buffer_size=%lldMB(old %lldMB)"
            ", keep_alive_time=%lldseconds(old %lldseconds)",
            _slice_quota_in == -1 ? -1 : _slice_quota_in * _slice_count / (1024L * 1024L),
            old_slice_quota_in == -1 ? -1 : old_slice_quota_in * _slice_count / (1024L * 1024L),
            _slice_quota_out == -1 ? -1 : _slice_quota_out * _slice_count / (1024L * 1024L),
            old_slice_quota_out == -1 ? -1 : old_slice_quota_out * _slice_count / (1024L * 1024L),
            _max_pending_buffer_size / (1024L * 1024L),
            old_max_pending_buffer_size / (1024L * 1024L),
            _keep_alive_ticks == -1 ? -1 : _keep_alive_ticks / _ticks_per_second,
            old_keep_alive_ticks == -1 ? -1 : old_keep_alive_ticks / _ticks_per_second);
#endif
}

bool RpcServerImpl::RegisterService(google::protobuf::Service* service, bool take_ownership)
{
    return _service_pool->RegisterService(service, take_ownership);
}

int RpcServerImpl::ServiceCount()
{
    return _service_pool->ServiceCount();
}

int RpcServerImpl::ConnectionCount()
{
    ScopedLocker<FastLock> _(_stream_list_lock);
    return _stream_list.size();
}

void RpcServerImpl::GetPendingStat(int64* pending_message_count,
        int64* pending_buffer_size, int64* pending_data_size)
{
    ScopedLocker<FastLock> _(_stream_list_lock);
    int64 message_count = 0;
    int64 buffer_size = 0;
    int64 data_size = 0;
    for (StreamList::iterator it = _stream_list.begin();
            it != _stream_list.end(); ++it)
    {
        message_count += (*it)->pending_message_count();
        buffer_size += (*it)->pending_buffer_size();
        data_size += (*it)->pending_data_size();
    }
    *pending_message_count = message_count;
    *pending_buffer_size = buffer_size;
    *pending_data_size = data_size;
}

bool RpcServerImpl::IsListening()
{
    ScopedLocker<MutexLock> _(_start_stop_lock);
    return _is_running && !_listener->is_closed();
}

bool RpcServerImpl::RestartListen()
{
    ScopedLocker<MutexLock> _(_start_stop_lock);
    if (!_is_running)
    {
#if defined( LOG )
        LOG(ERROR) << "RestartListen(): server not in running";
#else
        SLOG(ERROR, "RestartListen(): server not in running");
#endif
        return false;
    }

    // close listener first
    _listener->close();

    // reset and restart listener
    _listener.reset(new RpcListener(_work_thread_group->io_service(), _listen_endpoint));
    _listener->set_create_callback(boost::bind(
                &RpcServerImpl::OnCreated, shared_from_this(), _1));
    _listener->set_accept_callback(boost::bind(
                &RpcServerImpl::OnAccepted, shared_from_this(), _1));
    _listener->set_accept_fail_callback(boost::bind(
                &RpcServerImpl::OnAcceptFailed, shared_from_this(), _1, _2));
    if (!_listener->start_listen())
    {
#if defined( LOG )
        LOG(ERROR) << "RestartListen(): listen failed: " << _server_address;
#else
        SLOG(ERROR, "RestartListen(): listen failed: %s", _server_address.c_str());
#endif
        return false;
    }

#if defined( LOG )
    LOG(INFO) << "RestartListen(): restart listener succeed";
#else
    SLOG(INFO, "RestartListen(): restart listener succeed");
#endif
    return true;
}

void RpcServerImpl::OnCreated(const RpcServerStreamPtr& stream)
{
    stream->set_flow_controller(_flow_controller);
    stream->set_received_request_callback(
            boost::bind(&RpcServerImpl::OnReceived,
                shared_from_this(), _1, _2, _3, _4, _5, _6));
}

void RpcServerImpl::OnAccepted(const RpcServerStreamPtr& stream)
{
    if (!_is_running)
    {
        stream->close("server not running");
        return;
    }

    stream->set_max_pending_buffer_size(_max_pending_buffer_size);
    stream->reset_ticks((ptime_now() - _epoch_time).ticks());

    ScopedLocker<FastLock> _(_stream_list_lock);
    _stream_list.push_back(stream);
}

void RpcServerImpl::OnAcceptFailed(RpcErrorCode error_code, const std::string& error_text)
{
    if (!_is_running)
        return;

    // callback
    if (_event_handler)
    {
        _event_handler->NotifyAcceptFailed(error_code, error_text);
    }
}

void RpcServerImpl::OnReceived(
        const RpcEndpoint& local_endpoint,
        const RpcEndpoint& remote_endpoint,
        const RpcMeta& meta,
        const RpcServerStreamWPtr& stream,
        const ReadBufferPtr& buffer,
        int64 /* data_size */)
{
    if (!_is_running)
    {
#if defined( LOG )
        LOG(ERROR) << "OnReceived(): " << RpcEndpointToString(remote_endpoint)
                   << ": {" << meta.sequence_id() << "}: server not in running, ignore";
#else
        SLOG(ERROR, "OnReceived(): %s: {%lu}: server not in running, ignore",
                RpcEndpointToString(remote_endpoint).c_str(), meta.sequence_id());
#endif
        return;
    }

    if (!meta.has_method())
    {
#if defined( LOG )
        LOG(ERROR) << "OnReceived(): " << RpcEndpointToString(remote_endpoint)
                   << ": {" << meta.sequence_id() << "}: \"method\" field not set in meta";
#else
        SLOG(ERROR, "OnReceived(): %s: {%lu}: \"method\" field not set in meta",
                RpcEndpointToString(remote_endpoint).c_str(), meta.sequence_id());
#endif
        SendFailedResponse(stream, meta.sequence_id(),
                RPC_ERROR_NOT_SPECIFY_METHOD_NAME, "rpc meta: \"method\" field not set");
        return;
    }

    const std::string& method_full_name = meta.method();
    std::string service_name;
    std::string method_name;
    if (!ParseMethodFullName(method_full_name, &service_name, &method_name))
    {
#if defined( LOG )
        LOG(ERROR) << "OnReceived(): " << RpcEndpointToString(remote_endpoint)
                   << ": {" << meta.sequence_id() << "}"
                   << ": invalid method full name: " << method_full_name;
#else
        SLOG(ERROR, "OnReceived(): %s: {%lu}: invalid method full name: %s",
                RpcEndpointToString(remote_endpoint).c_str(), meta.sequence_id(),
                method_full_name.c_str());
#endif
        SendFailedResponse(stream, meta.sequence_id(),
                RPC_ERROR_PARSE_METHOD_NAME, "method full name: " + method_full_name);
        return;
    }

    ServiceBoard* service_board = _service_pool->FindService(service_name);
    if (service_board == NULL)
    {
#if defined( LOG )
        LOG(ERROR) << "OnReceived(): " << RpcEndpointToString(remote_endpoint)
                   << ": {" << meta.sequence_id() << "}"
                   << ": service \"" << service_name << "\" not found"
                   << ", method full name is \"" << method_full_name << "\"";
#else
        SLOG(ERROR, "OnReceived(): %s: {%lu}: "
                "service \"%s\" not found, method full name is \"%s\"",
                RpcEndpointToString(remote_endpoint).c_str(), meta.sequence_id(),
                service_name.c_str(), method_full_name.c_str());
#endif
        SendFailedResponse(stream, meta.sequence_id(),
                RPC_ERROR_FOUND_SERVICE, "method full name: " + method_full_name);
        return;
    }

    google::protobuf::Service* service = service_board->Service();
    const google::protobuf::MethodDescriptor* method =
        service->GetDescriptor()->FindMethodByName(method_name);
    if (method == NULL)
    {
#if defined( LOG )
        LOG(ERROR) << "OnReceived(): " << RpcEndpointToString(remote_endpoint)
                   << ": {" << meta.sequence_id() << "}"
                   << ": method \"" << method_name << "\" not found"
                   << ", method full name is \"" << method_full_name << "\"";
#else
        SLOG(ERROR, "OnReceived(): %s: {%lu}: "
                "method \"%s\" not found, method full name is \"%s\"",
                RpcEndpointToString(remote_endpoint).c_str(), meta.sequence_id(),
                method_name.c_str(), method_full_name.c_str());
#endif
        SendFailedResponse(stream, meta.sequence_id(),
                RPC_ERROR_FOUND_METHOD, "method full name: " + method_full_name);
        return;
    }

    google::protobuf::Message* request = service->GetRequestPrototype(method).New();
    SCHECK(request != NULL);
    CompressType compress_type = meta.has_compress_type() ? meta.compress_type(): CompressTypeNone;
    bool parse_request_return = false;
    if (compress_type == CompressTypeNone)
    {
        parse_request_return = request->ParseFromZeroCopyStream(buffer.get());
    }
    else
    {
        ::sofa::pbrpc::scoped_ptr<AbstractCompressedInputStream> is(
                get_compressed_input_stream(buffer.get(), compress_type));
        parse_request_return = request->ParseFromZeroCopyStream(is.get());
    }
    if (!parse_request_return)
    {
#if defined( LOG )
        LOG(ERROR) << "OnReceived(): " << RpcEndpointToString(remote_endpoint)
                   << ": {" << meta.sequence_id() << "}: parse request message failed";
#else
        SLOG(ERROR, "OnReceived(): %s: {%lu}: parse request message failed",
                RpcEndpointToString(remote_endpoint).c_str(), meta.sequence_id());
#endif
        SendFailedResponse(stream, meta.sequence_id(),
                RPC_ERROR_PARSE_REQUEST_MESSAGE, "method full name: " + method_full_name);
        delete request;
        return;
    }

    google::protobuf::Message* response = service->GetResponsePrototype(method).New();
    SCHECK(response != NULL);

    RpcController* controller = new RpcController();
    SCHECK(controller != NULL);
    const RpcControllerImplPtr& cntl = controller->impl();
    cntl->SetSequenceId(meta.sequence_id());
    cntl->SetMethodId(method_full_name);
    cntl->SetLocalEndpoint(local_endpoint);
    cntl->SetRemoteEndpoint(remote_endpoint);
    cntl->SetRpcServerStream(stream);
    cntl->SetRequestReceivedTime(ptime_now());
    cntl->SetResponseCompressType(meta.has_expected_response_compress_type() ?
            meta.expected_response_compress_type() : CompressTypeNone);

    MethodBoard* method_board = service_board->Method(method->index());
    method_board->ReportProcessBegin();

    google::protobuf::Closure* done = NewClosure(
            &RpcServerImpl::OnCallMethodDone, controller, request, response,
            method_board, ptime_now());
    service->CallMethod(method, controller, request, response, done);
}

void RpcServerImpl::OnCallMethodDone(
        RpcController* controller,
        google::protobuf::Message* request,
        google::protobuf::Message* response,
        MethodBoard* method_board,
        PTime start_time)
{
    int64 process_time_us = (ptime_now() - start_time).total_microseconds();
    const RpcControllerImplPtr& cntl = controller->impl();
    if (cntl->Failed())
    {
#if defined( LOG )
        LOG(ERROR) << "OnCallMethodDone(): call method \"" << cntl->MethodId()
                   << "\" failed: " << RpcErrorCodeToString(cntl->ErrorCode())
                   << ": " << cntl->Reason();
#else
        SLOG(ERROR, "OnCallMethodDone(): call method \"%s\" failed: %s: %s",
                cntl->MethodId().c_str(),
                RpcErrorCodeToString(cntl->ErrorCode()),
                cntl->Reason().c_str());
#endif
        method_board->ReportProcessEnd(false, process_time_us);
        SendFailedResponse(cntl->RpcServerStream(), cntl->SequenceId(),
                cntl->ErrorCode(), cntl->Reason());
    }
    else
    {
#if defined( LOG )
#else
        SLOG(DEBUG, "OnCallMethodDone(): call method \"%s\" succeed",
                cntl->MethodId().c_str());
#endif
        method_board->ReportProcessEnd(true, process_time_us);
        SendSucceedResponse(cntl->RpcServerStream(), cntl->SequenceId(),
                cntl->ResponseCompressType(), response);
    }

    delete request;
    delete response;
    delete controller;
}

void RpcServerImpl::SendFailedResponse(
        const RpcServerStreamWPtr& stream,
        uint64 sequence_id,
        int32 error_code,
        const std::string& reason)
{
    RpcServerStreamPtr real_stream = stream.lock();
    if (!real_stream)
    {
#if defined( LOG )
        LOG(ERROR) << "SendFailedResponse(): {" << sequence_id << "}: stream already closed";
#else
        SLOG(ERROR, "SendFailedResponse(): {%lu}: stream already closed", sequence_id);
#endif
        return;
    }

    RpcMeta meta;
    meta.set_type(RpcMeta::RESPONSE);
    meta.set_sequence_id(sequence_id);
    meta.set_failed(true);
    meta.set_error_code(error_code);
    meta.set_reason(reason);

    RpcMessageHeader header;
    int header_size = static_cast<int>(sizeof(header));
    WriteBuffer write_buffer;
    int64 header_pos = write_buffer.Reserve(header_size);
    if (header_pos < 0)
    {
#if defined( LOG )
        LOG(ERROR) << "SendFailedResponse(): {" << sequence_id << "}"
                   << ": reserve rpc message header failed";
#else
        SLOG(ERROR, "SendFailedResponse(): {%lu}: reserve rpc message header failed", sequence_id);
#endif
        return;
    }
    if (!meta.SerializeToZeroCopyStream(&write_buffer))
    {
#if defined( LOG )
        LOG(ERROR) << "SendFailedResponse(): {" << sequence_id << "}"
                   << ": serialize rpc meta failed";
#else
        SLOG(ERROR, "SendFailedResponse(): {%lu}: serialize rpc meta failed", sequence_id);
#endif
        return;
    }
    header.meta_size = static_cast<int>(write_buffer.ByteCount() - header_pos - header_size);
    header.data_size = 0;
    header.message_size = header.meta_size + header.data_size;
    write_buffer.SetData(header_pos, reinterpret_cast<const char*>(&header), header_size);

    ReadBufferPtr read_buffer(new ReadBuffer());
    write_buffer.SwapOut(read_buffer.get());

    real_stream->send_response(read_buffer,
            boost::bind(&RpcServerImpl::OnSendResponseDone,
                real_stream->remote_endpoint(), sequence_id, _1));
}

void RpcServerImpl::SendSucceedResponse(
        const RpcServerStreamWPtr& stream,
        uint64 sequence_id,
        CompressType compress_type,
        google::protobuf::Message* response)
{
    RpcServerStreamPtr real_stream = stream.lock();
    if (!real_stream)
    {
#if defined( LOG )
        LOG(ERROR) << "SendSucceedResponse(): {" << sequence_id << "}"
                   << ": stream already closed";
#else
        SLOG(ERROR, "SendSucceedResponse(): {%lu}: stream already closed", sequence_id);
#endif
        return;
    }

    RpcMeta meta;
    meta.set_type(RpcMeta::RESPONSE);
    meta.set_sequence_id(sequence_id);
    meta.set_failed(false);
    meta.set_compress_type(compress_type);

    RpcMessageHeader header;
    int header_size = static_cast<int>(sizeof(header));
    WriteBuffer write_buffer;
    int64 header_pos = write_buffer.Reserve(header_size);
    if (header_pos < 0)
    {
#if defined( LOG )
        LOG(ERROR) << "SendSucceedResponse(): {" << sequence_id << "}"
                   << ": reserve rpc message header failed";
#else
        SLOG(ERROR, "SendSucceedResponse(): {%lu}: reserve rpc message header failed", sequence_id);
#endif
        return;
    }
    if (!meta.SerializeToZeroCopyStream(&write_buffer))
    {
#if defined( LOG )
        LOG(ERROR) << "SendSucceedResponse(): {" << sequence_id << "}"
                   << ": serialize rpc meta failed";
#else
        SLOG(ERROR, "SendSucceedResponse(): {%lu}: serialize rpc meta failed", sequence_id);
#endif
        SendFailedResponse(stream, sequence_id,
                RPC_ERROR_SERIALIZE_RESPONSE, "serialize rpc meta failed");
        return;
    }
    header.meta_size = static_cast<int>(write_buffer.ByteCount() - header_pos - header_size);
    bool serialize_response_return = false;
    if (compress_type == CompressTypeNone)
    {
        serialize_response_return = response->SerializeToZeroCopyStream(&write_buffer);
    }
    else
    {
        ::sofa::pbrpc::scoped_ptr<AbstractCompressedOutputStream> os(
                get_compressed_output_stream(&write_buffer, compress_type));
        serialize_response_return = response->SerializeToZeroCopyStream(os.get());
        os->Flush();
    }
    if (!serialize_response_return)
    {
#if defined( LOG )
        LOG(ERROR) << "SendSucceedResponse(): {" << sequence_id << "}"
                   << ": serialize response message failed";
#else
        SLOG(ERROR, "SendSucceedResponse(): {%lu}: serialize response message failed", sequence_id);
#endif
        SendFailedResponse(stream, sequence_id,
                RPC_ERROR_SERIALIZE_RESPONSE, "serialize response message failed");
        return;
    }
    header.data_size = write_buffer.ByteCount() - header_pos - header_size - header.meta_size;
    header.message_size = header.meta_size + header.data_size;
    write_buffer.SetData(header_pos, reinterpret_cast<const char*>(&header), header_size);

    ReadBufferPtr read_buffer(new ReadBuffer());
    write_buffer.SwapOut(read_buffer.get());

    real_stream->send_response(read_buffer,
            boost::bind(&RpcServerImpl::OnSendResponseDone,
                real_stream->remote_endpoint(), sequence_id, _1));
}

void RpcServerImpl::OnSendResponseDone(
        const RpcEndpoint& remote_endpoint,
        uint64 sequence_id,
        RpcErrorCode error_code)
{
    if (error_code == RPC_SUCCESS)
    {
#if defined( LOG )
#else
        SLOG(DEBUG, "OnSendResponseDone(): %s {%lu}: send succeed",
                RpcEndpointToString(remote_endpoint).c_str(), sequence_id);
#endif
    }
    else
    {
#if defined( LOG )
        LOG(ERROR) << "OnSendResponseDone(): " << RpcEndpointToString(remote_endpoint)
                   << " {" << sequence_id << "}"
                   << ": send failed: " << RpcErrorCodeToString(error_code);
#else
        SLOG(ERROR, "OnSendResponseDone(): %s {%lu}: send failed: %s",
                RpcEndpointToString(remote_endpoint).c_str(), sequence_id,
                RpcErrorCodeToString(error_code));
#endif
    }
}

void RpcServerImpl::StopStreams()
{
    ScopedLocker<FastLock> _(_stream_list_lock);
    for (StreamList::iterator it = _stream_list.begin();
            it != _stream_list.end(); ++it)
    {
        (*it)->close("server stopped");
    }
}

void RpcServerImpl::ClearStreams()
{
    ScopedLocker<FastLock> _(_stream_list_lock);
    _stream_list.clear();
}

void RpcServerImpl::TimerMaintain(const PTime& now)
{
    SOFA_PBRPC_FUNCTION_TRACE;

    int64 now_ticks = (now - _epoch_time).ticks();

    // check listener, if closed, then try to restart it every interval.
    if (_listener->is_closed()
            && now_ticks - _last_restart_listen_ticks >= _restart_listen_interval_ticks)
    {
        _last_restart_listen_ticks = now_ticks;
        RestartListen();
    }

    // check streams, if closed, then remove from list
    std::list<RpcServerStreamPtr> live_streams;
    std::list<RpcServerStreamPtr> closed_streams;
    int live_count = 0;
    {
        ScopedLocker<FastLock> _(_stream_list_lock);
        for (StreamList::iterator it = _stream_list.begin();
                it != _stream_list.end(); )
        {
            const RpcServerStreamPtr& stream = *it;

            if (_keep_alive_ticks != -1
                    && now_ticks - stream->last_rw_ticks() >= _keep_alive_ticks)
            {
                stream->close("keep alive timeout");
            }

            if (stream->is_closed())
            {
                closed_streams.push_back(stream);
                it = _stream_list.erase(it);
            }
            else
            {
                stream->reset_ticks(now_ticks);
                live_streams.push_back(stream);
                ++live_count;
                ++it;
            }
        }
    }
    _live_stream_count = live_count;

    // flow control
    if (_slice_quota_in != -1)
    {
        // reset quota pool
        _flow_controller->reset_read_quota(_slice_quota_in);

        // collect streams need to trigger
        std::vector<FlowControlItem> trigger_list;
        for (std::list<RpcServerStreamPtr>::iterator it = live_streams.begin();
                it != live_streams.end(); ++it)
        {
            int token = (*it)->read_quota_token();
            if (token <= 0)
            {
                // only need trigger streams whose token <= 0
                trigger_list.push_back(FlowControlItem(token, (*it).get()));
            }
        }

        // sort by token: token closer to zero, earlier to trigger
        std::sort(trigger_list.begin(), trigger_list.end());

        // trigger in order
        for (std::vector<FlowControlItem>::iterator t_it = trigger_list.begin();
                t_it != trigger_list.end(); ++t_it)
        {
            t_it->stream->trigger_receive();
        }
    }
    if (_slice_quota_out != -1)
    {
        // reset quota pool
        _flow_controller->reset_write_quota(_slice_quota_out);

        // collect streams need to trigger
        std::vector<FlowControlItem> trigger_list;
        for (std::list<RpcServerStreamPtr>::iterator it = live_streams.begin();
                it != live_streams.end(); ++it)
        {
            int token = (*it)->write_quota_token();
            if (token <= 0)
            {
                // only need trigger streams whose token <= 0
                trigger_list.push_back(FlowControlItem(token, (*it).get()));
            }
        }

        // sort by token: token closer to zero, earlier to trigger
        std::sort(trigger_list.begin(), trigger_list.end());

        // trigger in order
        for (std::vector<FlowControlItem>::iterator t_it = trigger_list.begin();
                t_it != trigger_list.end(); ++t_it)
        {
            t_it->stream->trigger_send();
        }
    }

    if (now_ticks - _last_switch_stat_slot_ticks >= _switch_stat_slot_interval_ticks)
    {
        _last_switch_stat_slot_ticks = now_ticks;
        _service_pool->NextSlot();
    }

    if (now_ticks - _last_print_connection_ticks >= _print_connection_interval_ticks)
    {
        _last_print_connection_ticks = now_ticks;
#if defined( LOG )
        LOG(INFO) << "TimerMaintain(): countof(RpcListener)="
                  << SOFA_PBRPC_GET_RESOURCE_COUNTER(RpcListener)
                  << ", countof(RpcByteStream)=" << SOFA_PBRPC_GET_RESOURCE_COUNTER(RpcByteStream)
                  << ", live_stream_count=" << _live_stream_count;
#else
        SLOG(INFO, "TimerMaintain(): countof(RpcListener)=%ld"
                ", countof(RpcByteStream)=%ld, live_stream_count=%d",
                SOFA_PBRPC_GET_RESOURCE_COUNTER(RpcListener),
                SOFA_PBRPC_GET_RESOURCE_COUNTER(RpcByteStream),
                _live_stream_count);
#endif
    }

    _last_maintain_ticks = now_ticks;
}

bool RpcServerImpl::ParseMethodFullName(const std::string& method_full_name,
        std::string* service_full_name, std::string* method_name)
{
    std::string::size_type pos = method_full_name.rfind('.');
    if (pos == std::string::npos) return false;
    *service_full_name = method_full_name.substr(0, pos);
    *method_name = method_full_name.substr(pos + 1);
    return true;
}

} // namespace pbrpc
} // namespace sofa

/* vim: set ts=4 sw=4 sts=4 tw=100 */
