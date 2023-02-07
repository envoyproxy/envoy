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

        fn register_future_with_executor(executor: Pin<&mut Executor>, future: Box<FutureHandle>);

        type WaitForDataHandle;

        fn notify_waiting_for_data(executor: Pin<&mut Executor>) -> Pin<&WaitForDataHandle>;
    }

    extern "Rust" {
        type EchoFilter<'a>;

        fn create_filter(read_callbacks: Pin<&mut ReadFilterCallbacks>) -> Box<EchoFilter<'_>>;

        fn on_new_connection(filter: &mut EchoFilter, executor: Pin<&mut Executor>);

        type FutureHandle;
    }
}

use crate::ffi::{ReadFilterCallbacks, Executor};
use std::pin::Pin;
use std::future::Future;
use std::task::{Context, Poll};

struct EchoFilter<'a> {
    read_callbacks: Pin<&'a mut ReadFilterCallbacks>,
}

fn create_filter(read_callbacks: Pin<&mut ReadFilterCallbacks>) -> Box<EchoFilter<'_>> {
    Box::new(EchoFilter { read_callbacks })
}

fn register_future<'a>(executor: Pin<&'a mut Executor>, future: impl Future<Output = ()> + 'a) {
    ffi::register_future_with_executor(executor, Box::new(FutureHandle { future: Box::new(future) }))
}

fn on_new_connection(_filter: &mut EchoFilter, mut executor: Pin<&mut Executor>) {
   register_future(executor.as_mut(), on_new_connection_async(FilterApi { executor }))
}

struct FilterApi<'a> {
    executor: Pin<&'a mut Executor>
}

impl<'a> FilterApi<'a> {
    fn data(&mut self) -> DataFuture<'a> {
        let handle = ffi::notify_waiting_for_data(self.executor.as_mut());

        DataFuture { handle }
    }
}

struct DataFuture<'a> {
    handle: Pin<&'a ffi::WaitForDataHandle>
}

impl<'a> Future for DataFuture<'a> {
    type Output = (&'a[u8], bool);
fn poll(self: Pin<&mut Self>, _: &mut Context<'_>) -> Poll<<Self as Future>::Output> { todo!() }
}

async fn on_new_connection_async(mut api: FilterApi<'_>) {
    let (data, end_stream) = api.data().await;
}

pub struct FutureHandle {
    future: Box<dyn Future<Output = ()>>,
}
