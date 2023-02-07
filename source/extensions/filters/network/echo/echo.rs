#[cxx::bridge]
mod ffi {
    unsafe extern "C++" {
        include!("envoy/network/filter.h");
        include!("envoy/buffer/buffer.h");

        #[namespace = "Envoy::Network"]
        type ReadFilterCallbacks;

        #[namespace = "Envoy::Buffer"]
        type Instance;

        #[namespace = "Envoy::Network"]
        type Connection;

        include!("source/extensions/filters/network/echo/rust_support.h");

        type Executor;

        unsafe fn register_future_with_executor(executor: *const Executor, future: Box<FutureHandle>);

        type WaitForDataHandle;

        unsafe fn notify_waiting_for_data(executor: *const Executor) -> *const WaitForDataHandle;

        unsafe fn wake_executor(executor: *const Executor);

        unsafe fn drop_executor(executor: *const Executor);

        unsafe fn data_available(data_handle: *const WaitForDataHandle) -> bool;

        unsafe fn data_as_slice(data_handle: *const WaitForDataHandle) -> *mut Instance;

        unsafe fn is_end_stream(data_handle: *const WaitForDataHandle) -> bool;

        unsafe fn connection(read_callbacks: *mut ReadFilterCallbacks) -> *mut Connection;

        unsafe fn write_to(connection: *mut Connection, data: *mut Instance, end_stream: bool);
    }

    extern "Rust" {
        unsafe fn on_new_connection(filter: *mut ReadFilterCallbacks, executor: *const Executor);

        type FutureHandle;

        fn poll_future(future: &mut FutureHandle);

        type Runtime;
    }
}

use crate::ffi::{ReadFilterCallbacks, Executor, Instance};
use std::pin::Pin;
use std::future::Future;
use std::task::{Context, Poll, RawWaker, Waker, RawWakerVTable};

struct Runtime {
    waker: Waker
}

fn create_raw_waker(executor: *const ()) -> RawWaker {
        RawWaker::new(executor, &RawWakerVTable::new(create_raw_waker, |executor| { unsafe { ffi::wake_executor(executor.cast()) } }, |executor| { unsafe { ffi::wake_executor(executor.cast()) } }, |executor| { unsafe { ffi::drop_executor(executor.cast()) } }))
}

impl Runtime {
    fn new(executor: *const Executor) -> Self {

        Self { waker: unsafe { Waker::from_raw(create_raw_waker(executor.cast()) ) } }
    }
}

fn register_future(executor: *const Executor, future: impl Future<Output = ()> + 'static) {
    unsafe { ffi::register_future_with_executor(executor, Box::new(FutureHandle { future: Box::pin(future), waker: Some(Waker::from_raw(create_raw_waker(executor.cast()))) })) }
}

fn on_new_connection(read_callbacks: *mut ReadFilterCallbacks, executor: *const Executor) {
   register_future(executor, on_new_connection_async(FilterApi { read_callbacks, executor }))
}

struct FilterApi {
    executor: *const Executor,
    read_callbacks: *mut ReadFilterCallbacks,
}

impl FilterApi {
    fn data(&mut self) -> DataFuture {
        let handle = unsafe { ffi::notify_waiting_for_data(self.executor) };

        DataFuture { handle }
    }

    fn connection(&mut self) -> *mut ffi::Connection {
        unsafe {
       ffi::connection(self.read_callbacks) 
        }
    }
}

struct DataFuture {
    handle: *const ffi::WaitForDataHandle
}

impl Future for DataFuture {
    type Output = (*mut Instance, bool);
fn poll(self: Pin<&mut Self>, _: &mut Context<'_>) -> Poll<<Self as Future>::Output> { 
    unsafe {
    if ffi::data_available(self.handle)   {
        Poll::Ready((ffi::data_as_slice(self.handle), ffi::is_end_stream(self.handle))) 
    } else {
    Poll::Pending
        }
    }
}
}

async fn on_new_connection_async(mut api: FilterApi) {
    let connection = api.connection();

    loop {
        let (data, end_stream) = api.data().await;
        eprintln!("got data, es={end_stream}");

        unsafe { ffi::write_to(connection, data, end_stream) };
    }
}

pub struct FutureHandle {
    future: Pin<Box<dyn Future<Output = ()>>>,
    waker: Option<Waker>,
}

        fn poll_future(future: &mut FutureHandle) {
            if future.future.as_mut().poll(&mut Context::from_waker(future.waker.as_ref().unwrap())).is_ready() {
                future.waker.take().unwrap().wake();
            }
        }
