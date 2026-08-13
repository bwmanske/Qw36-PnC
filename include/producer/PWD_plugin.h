#ifndef PC_PWD_PLUGIN_H
#define PC_PWD_PLUGIN_H

#include "producer/test_plugin.h"

namespace pc {

pc::TestPlugin create_pwd_plugin();
void pwd_set_found(const std::string& password);
void pwd_set_file_error(const std::string& msg);
const std::string& pwd_get_found_password();
const std::string& pwd_get_file_error();

} // namespace pc

#endif // PC_PWD_PLUGIN_H
