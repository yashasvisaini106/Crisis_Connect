#include <thread>
#include <mutex>
int main(){ std::mutex m; std::thread t([]{}); t.join(); return 0; }
