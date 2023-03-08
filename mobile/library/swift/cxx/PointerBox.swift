/// Boxes a generic value of type `T` and provides accessors to convert to and from stable pointers.
final class PointerBox<T> {
  let value: T

  init(value: T) {
    self.value = value
  }

  func retainedMutablePointer() -> UnsafeMutableRawPointer {
    Unmanaged.passRetained(self).toOpaque()
  }

  func retainedPointer() -> UnsafeRawPointer {
    UnsafeRawPointer(Unmanaged.passRetained(self).toOpaque())
  }

  static func unretained(from pointer: UnsafeRawPointer) -> T {
    Self.unretained(from: UnsafeMutableRawPointer(mutating: pointer))
  }

  static func unretained(from pointer: UnsafeMutableRawPointer) -> T {
    Self.unmanaged(from: pointer).takeUnretainedValue().value
  }

  static func unmanaged(from pointer: UnsafeRawPointer) -> Unmanaged<PointerBox<T>> {
    Unmanaged<PointerBox<T>>.fromOpaque(UnsafeMutableRawPointer(mutating: pointer))
  }

  static func unmanaged(from pointer: UnsafeMutableRawPointer) -> Unmanaged<PointerBox<T>> {
    Unmanaged<PointerBox<T>>.fromOpaque(pointer)
  }
}
