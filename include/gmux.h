#pragma once

#include "stdafx.h"

int ServerMain(std::wstring const &pipe_name, HINSTANCE hInstance);
int ClientMain(std::wstring const &pipe_name);
