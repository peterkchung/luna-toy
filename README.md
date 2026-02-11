# LunaToy

A 2D toy simulation built with Vulkan to help people build intuition on simulating projects related to the moon.

## Dependencies

- C++17
- Vulkan API
- GLFW (windowing)
- GLM (math)
- CMake

## Build Commands

```bash
cmake -B build
cmake --build build
./build/luna
```

Re-run `cmake -B build` after modifying `CMakeLists.txt`. Otherwise, `cmake --build build` is all you need.

## Project Structure

```
luna/
├── src/
│   └── main.cpp
├── CMakeLists.txt
└── README.md
```

## License

Apache 2.0. Please refer to [LICENSE](LICENSE) for details.
