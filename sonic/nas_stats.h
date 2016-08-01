
/*
 * Copyright (c) 2016 Dell Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License"); you may
 * not use this file except in compliance with the License. You may obtain
 * a copy of the License at http://www.apache.org/licenses/LICENSE-2.0
 *
 * THIS CODE IS PROVIDED ON AN  *AS IS* BASIS, WITHOUT WARRANTIES OR
 * CONDITIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING WITHOUT
 *  LIMITATION ANY IMPLIED WARRANTIES OR CONDITIONS OF TITLE, FITNESS
 * FOR A PARTICULAR PURPOSE, MERCHANTABLITY OR NON-INFRINGEMENT.
 *
 * See the Apache Version 2.0 License for specific language governing
 * permissions and limitations under the License.
 */


/*
 * filename: nas_stats.h
 */


#ifndef NAS_STATS_H_
#define NAS_STATS_H_

#include "std_error_codes.h"
#include "cps_api_operation.h"

#ifdef __cplusplus
extern "C" {
#endif

t_std_error nas_stats_if_init(cps_api_operation_handle_t handle);

t_std_error nas_stats_vlan_init(cps_api_operation_handle_t handle);

bool nas_stat_get_ifindex_from_obj(cps_api_object_t obj,hal_ifindex_t *index,bool clear);

#ifdef __cplusplus
}
#endif

#endif /* NAS_STATS_H_ */
