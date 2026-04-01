#include "plugin_interface.h"
#include <cmath>
#include <stdexcept>

double calc_ln(double val) {
    if (val <= 0) {
        throw std::domain_error("ln(x) is undefined for x <= 0");
    }
    return log(val);
}

extern "C" PLUGIN_API PluginInfo GetPluginInfo() {
    PluginInfo info;
    info.functionName = "ln";
    info.functionPtr = calc_ln;
    return info;
}