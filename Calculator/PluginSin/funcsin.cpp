#include "plugin_interface.h"
#include <cmath>

// Функция: синус от градуса
double calc_sin_deg(double degrees) {
    double radians = degrees * 3.14159265358979323846 / 180.0;
    return sin(radians);
}

// Точка входа, обязательная для всех плагинов
extern "C" PLUGIN_API PluginInfo GetPluginInfo() {
    PluginInfo info;
    info.functionName = "sin";
    info.functionPtr = calc_sin_deg;
    return info;
}