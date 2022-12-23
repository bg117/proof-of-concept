#pragma once

#ifdef FATFS_ALLOW_PRIV_NS
#define FATFS_BEGIN_PRIV_NS namespace fatfs { namespace priv {
#define FATFS_END_PRIV_NS } }
#endif