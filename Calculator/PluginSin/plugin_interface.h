#pragma once
#pragma once
#ifndef PLUGIN_INTERFACE_H
#define PLUGIN_INTERFACE_H

#ifdef _WIN32
#define PLUGIN_API __declspec(dllexport)
#else
#define PLUGIN_API
#endif

#include <cmath>

// Тип функции, которую предоставляет плагин (принимает double, возвращает double)
typedef double (*MathFunction)(double);

// Структура метаданных, которую плагин возвращает при загрузке
struct PluginInfo {
    const char* functionName; // Имя функции для использования в выражении (например, "sin")
    MathFunction functionPtr; // Указатель на саму функцию
};

// Имя экспортируемой функции, которую ищет загрузчик
#define PLUGIN_EXPORT_NAME "GetPluginInfo"

// Тип экспортируемой функции
typedef PluginInfo(*GetPluginInfoFunc)();

#endif