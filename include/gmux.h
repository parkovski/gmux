#pragma once

#include "stdafx.h"

const DWORD PIPE_BUFFER_SIZE = 256;

int ServerMain(std::wstring const &pipe_name, HINSTANCE hInstance);
int ClientMain(std::wstring const &pipe_name);
