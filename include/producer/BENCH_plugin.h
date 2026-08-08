#ifndef PC_BENCH_PLUGIN_H
#define PC_BENCH_PLUGIN_H

#include "producer/test_plugin.h"

namespace pc {

pc::TestPlugin create_bench_plugin();
void set_bench_source_file(const std::string& path);

} // namespace pc

#endif // PC_BENCH_PLUGIN_H
