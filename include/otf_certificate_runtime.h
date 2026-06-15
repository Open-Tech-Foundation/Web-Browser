#ifndef OTF_CERTIFICATE_RUNTIME_H_
#define OTF_CERTIFICATE_RUNTIME_H_

#include "include/cef_base.h"

namespace otf {

bool IsCertificateErrorCode(cef_errorcode_t error_code);

}  // namespace otf

#endif  // OTF_CERTIFICATE_RUNTIME_H_
