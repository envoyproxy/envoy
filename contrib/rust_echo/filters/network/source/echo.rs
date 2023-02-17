#[cxx::bridge]
mod ffi {
    extern "C++" {
        include!("contrib/rust_echo/filters/network/source/executor.rs.h");

        #[namespace = "Envoy::Network"]
        type ReadFilterCallbacks = executor_rs::ffi::ReadFilterCallbacks;

        #[namespace = "Envoy::Extensions::NetworkFilters::Echo"]
        type Executor = executor_rs::ffi::Executor;
    }

    #[namespace = "Envoy::Extensions::NetworkFilters::Echo"]
    extern "Rust" {
        unsafe fn on_new_connection(filter: *mut ReadFilterCallbacks, executor: *const Executor);
    }
}

use crate::ffi::{Executor, ReadFilterCallbacks};
use executor_rs::FilterApi;

fn on_new_connection(read_callbacks: *mut ReadFilterCallbacks, executor: *const Executor) {
    executor_rs::register_future(
        executor,
        on_new_connection_async(FilterApi::new(executor, read_callbacks)),
    )
}

async fn on_new_connection_async(mut api: FilterApi) {
    let mut connection = api.connection();

    loop {
        let (data, end_stream) = api.data().await;

        connection.write(data, end_stream);
    }
}
