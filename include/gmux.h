#pragma once

#include "stdafx.h"

const DWORD PIPE_BUFFER_SIZE = 256;
const UINT MSG_SERVER_RECEIVE = WM_APP + 1;

int ServerMain(std::wstring const &pipe_name, HINSTANCE hInstance);
int ClientMain(std::wstring const &pipe_name, std::wstring const &args);
