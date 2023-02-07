#[cxx::bridge]
mod ffi {
    #[namespace = "Envoy::Network"]
    #[repr(u32)]
    enum FilterStatus {
        Continue,
        StopIteration,
    }

    unsafe extern "C++" {
        include!("envoy/network/filter.h");
        include!("envoy/buffer/buffer.h");

        #[namespace = "Envoy::Network"]
        type ReadFilterCallbacks;

        #[namespace = "Envoy::Buffer"]
        type Instance;

        #[namespace = "Envoy::Network"]
        type FilterStatus;

        #[namespace = "Envoy::Network"]
        type Connection;

        include!("source/extensions/filters/network/echo/rust_support.h");

        fn connection(read_callbacks: Pin<&mut ReadFilterCallbacks>) -> Pin<&mut Connection>;

        fn write(connection: Pin<&mut Connection>, data: Pin<&mut Instance>, end_stream: bool);
    }

    extern "Rust" {
        type EchoFilter<'a>;

        fn create_filter(read_callbacks: Pin<&mut ReadFilterCallbacks>) -> Box<EchoFilter<'_>>;

        fn on_new_connection(filter: &mut EchoFilter) -> FilterStatus;

        fn on_data(
            filter: &mut EchoFilter,
            buffer: Pin<&mut Instance>,
            end_stream: bool,
        ) -> FilterStatus;
    }
}

use crate::ffi::{connection, write, FilterStatus, Instance, ReadFilterCallbacks};
use std::pin::Pin;

struct EchoFilter<'a> {
    read_callbacks: Pin<&'a mut ReadFilterCallbacks>,
}

fn create_filter(read_callbacks: Pin<&mut ReadFilterCallbacks>) -> Box<EchoFilter<'_>> {
    Box::new(EchoFilter { read_callbacks })
}

fn on_new_connection(_filter: &mut EchoFilter) -> FilterStatus {
    FilterStatus::Continue
}

fn on_data(filter: &mut EchoFilter, buffer: Pin<&mut Instance>, end_stream: bool) -> FilterStatus {
    write(
        connection(filter.read_callbacks.as_mut()),
        buffer,
        end_stream,
    );

    FilterStatus::Continue
}
