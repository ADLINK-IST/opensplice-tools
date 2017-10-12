/* Copyright 2017 PrismTech Limited

   Licensed under the Apache License, Version 2.0 (the "License");
   you may not use this file except in compliance with the License.
   You may obtain a copy of the License at

       http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software
   distributed under the License is distributed on an "AS IS" BASIS,
   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
   See the License for the specific language governing permissions and
   limitations under the License. */
#ifndef __ospli_osplo__porting__
#define __ospli_osplo__porting__

#ifndef __GNUC__
#define __attribute__(x)
#endif

#if __SunOS_5_10 && ! defined NEED_STRSEP
#define NEED_STRSEP 1
#endif

#if NEED_STRSEP
char *strsep (char **str, const char *sep);
#endif

#endif /* defined(__ospli_osplo__porting__) */
