/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 4 -*-
 * vim: set ts=8 sw=4 et tw=78:
 *
 * ***** BEGIN LICENSE BLOCK *****
 * Version: MPL 1.1/GPL 2.0/LGPL 2.1
 *
 * The contents of this file are subject to the Mozilla Public License Version
 * 1.1 (the "License"); you may not use this file except in compliance with
 * the License. You may obtain a copy of the License at
 * http://www.mozilla.org/MPL/
 *
 * Software distributed under the License is distributed on an "AS IS" basis,
 * WITHOUT WARRANTY OF ANY KIND, either express or implied. See the License
 * for the specific language governing rights and limitations under the
 * License.
 *
 * The Original Code is Mozilla Communicator client code, released
 * March 31, 1998.
 *
 * The Initial Developer of the Original Code is
 * Netscape Communications Corporation.
 * Portions created by the Initial Developer are Copyright (C) 1998
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *   John Bandhauer <jband@netscape.com> (original author)
 *   Pierre Phaneuf <pp@ludusdesign.com>
 *
 * Alternatively, the contents of this file may be used under the terms of
 * either of the GNU General Public License Version 2 or later (the "GPL"),
 * or the GNU Lesser General Public License Version 2.1 or later (the "LGPL"),
 * in which case the provisions of the GPL or the LGPL are applicable instead
 * of those above. If you wish to allow use of your version of this file only
 * under the terms of either the GPL or the LGPL, and not to allow others to
 * use your version of this file under the terms of the MPL, indicate your
 * decision by deleting the provisions above and replace them with the notice
 * and other provisions required by the GPL or the LGPL. If you do not delete
 * the provisions above, a recipient may use your version of this file under
 * the terms of any one of the MPL, the GPL or the LGPL.
 *
 * ***** END LICENSE BLOCK ***** */

/* The "Components" xpcom objects for JavaScript. */

#include "mozilla/unused.h"

#include "xpcprivate.h"
#include "nsReadableUtils.h"
#include "xpcIJSModuleLoader.h"
#include "nsIScriptObjectPrincipal.h"
#include "nsIDOMWindow.h"
#include "XPCJSWeakReference.h"
#include "XPCWrapper.h"
#include "jsproxy.h"
#include "WrapperFactory.h"
#include "XrayWrapper.h"
#include "nsNullPrincipal.h"
#include "nsJSUtils.h"
#include "mozJSComponentLoader.h"
#include "nsContentUtils.h"
#include "jsgc.h"
#include "jsfriendapi.h"
#include "AccessCheck.h"
#include "mozilla/dom/bindings/Utils.h"

using namespace mozilla;
using namespace js;

using mozilla::dom::bindings::DestroyProtoOrIfaceCache;

/***************************************************************************/
// stuff used by all

static nsresult ThrowAndFail(unsigned errNum, JSContext* cx, bool* retval)
{
    XPCThrower::Throw(errNum, cx);
    *retval = false;
    return NS_OK;
}

static JSBool
JSValIsInterfaceOfType(JSContext *cx, jsval v, REFNSIID iid)
{
    nsCOMPtr<nsIXPConnect> xpc;
    nsCOMPtr<nsIXPConnectWrappedNative> wn;
    nsCOMPtr<nsISupports> sup;
    nsISupports* iface;
    if (!JSVAL_IS_PRIMITIVE(v) &&
        nsnull != (xpc = nsXPConnect::GetXPConnect()) &&
        NS_SUCCEEDED(xpc->GetWrappedNativeOfJSObject(cx, JSVAL_TO_OBJECT(v),
                                                     getter_AddRefs(wn))) && wn &&
        NS_SUCCEEDED(wn->Native()->QueryInterface(iid, (void**)&iface)) && iface) {
        NS_RELEASE(iface);
        return true;
    }
    return false;
}

char* xpc_CloneAllAccess()
{
    static const char allAccess[] = "AllAccess";
    return (char*)nsMemory::Clone(allAccess, sizeof(allAccess));
}

char * xpc_CheckAccessList(const PRUnichar* wideName, const char* list[])
{
    nsCAutoString asciiName;
    CopyUTF16toUTF8(nsDependentString(wideName), asciiName);

    for (const char** p = list; *p; p++)
        if (!strcmp(*p, asciiName.get()))
            return xpc_CloneAllAccess();

    return nsnull;
}

/***************************************************************************/
/***************************************************************************/
/***************************************************************************/



class nsXPCComponents_Interfaces :
            public nsIXPCComponents_Interfaces,
            public nsIXPCScriptable,
            public nsIClassInfo,
            public nsISecurityCheckedComponent
{
public:
    // all the interface method declarations...
    NS_DECL_ISUPPORTS
    NS_DECL_NSIXPCCOMPONENTS_INTERFACES
    NS_DECL_NSIXPCSCRIPTABLE
    NS_DECL_NSICLASSINFO
    NS_DECL_NSISECURITYCHECKEDCOMPONENT

public:
    nsXPCComponents_Interfaces();
    virtual ~nsXPCComponents_Interfaces();

private:
    nsCOMPtr<nsIInterfaceInfoManager> mManager;
};

/* void getInterfaces (out PRUint32 count, [array, size_is (count), retval]
                       out nsIIDPtr array); */
NS_IMETHODIMP
nsXPCComponents_Interfaces::GetInterfaces(PRUint32 *aCount, nsIID * **aArray)
{
    const PRUint32 count = 3;
    *aCount = count;
    nsIID **array;
    *aArray = array = static_cast<nsIID**>(nsMemory::Alloc(count * sizeof(nsIID*)));
    if (!array)
        return NS_ERROR_OUT_OF_MEMORY;

    PRUint32 index = 0;
    nsIID* clone;
#define PUSH_IID(id)                                                          \
    clone = static_cast<nsIID *>(nsMemory::Clone(&NS_GET_IID( id ),           \
                                                 sizeof(nsIID)));             \
    if (!clone)                                                               \
        goto oom;                                                             \
    array[index++] = clone;

    PUSH_IID(nsIXPCComponents_Interfaces)
    PUSH_IID(nsIXPCScriptable)
    PUSH_IID(nsISecurityCheckedComponent)
#undef PUSH_IID

    return NS_OK;
oom:
    while (index)
        nsMemory::Free(array[--index]);
    nsMemory::Free(array);
    *aArray = nsnull;
    return NS_ERROR_OUT_OF_MEMORY;
}

/* nsISupports getHelperForLanguage (in PRUint32 language); */
NS_IMETHODIMP
nsXPCComponents_Interfaces::GetHelperForLanguage(PRUint32 language,
                                                 nsISupports **retval)
{
    *retval = nsnull;
    return NS_OK;
}

/* readonly attribute string contractID; */
NS_IMETHODIMP
nsXPCComponents_Interfaces::GetContractID(char * *aContractID)
{
    *aContractID = nsnull;
    return NS_ERROR_NOT_AVAILABLE;
}

/* readonly attribute string classDescription; */
NS_IMETHODIMP
nsXPCComponents_Interfaces::GetClassDescription(char * *aClassDescription)
{
    static const char classDescription[] = "XPCComponents_Interfaces";
    *aClassDescription = (char*)nsMemory::Clone(classDescription, sizeof(classDescription));
    return *aClassDescription ? NS_OK : NS_ERROR_OUT_OF_MEMORY;
}

/* readonly attribute nsCIDPtr classID; */
NS_IMETHODIMP
nsXPCComponents_Interfaces::GetClassID(nsCID * *aClassID)
{
    *aClassID = nsnull;
    return NS_OK;
}

/* readonly attribute PRUint32 implementationLanguage; */
NS_IMETHODIMP
nsXPCComponents_Interfaces::GetImplementationLanguage(PRUint32 *aImplementationLanguage)
{
    *aImplementationLanguage = nsIProgrammingLanguage::CPLUSPLUS;
    return NS_OK;
}

/* readonly attribute PRUint32 flags; */
NS_IMETHODIMP
nsXPCComponents_Interfaces::GetFlags(PRUint32 *aFlags)
{
    *aFlags = nsIClassInfo::THREADSAFE;
    return NS_OK;
}

/* [notxpcom] readonly attribute nsCID classIDNoAlloc; */
NS_IMETHODIMP
nsXPCComponents_Interfaces::GetClassIDNoAlloc(nsCID *aClassIDNoAlloc)
{
    return NS_ERROR_NOT_AVAILABLE;
}

nsXPCComponents_Interfaces::nsXPCComponents_Interfaces() :
    mManager(do_GetService(NS_INTERFACEINFOMANAGER_SERVICE_CONTRACTID))
{
}

nsXPCComponents_Interfaces::~nsXPCComponents_Interfaces()
{
    // empty
}


NS_INTERFACE_MAP_BEGIN(nsXPCComponents_Interfaces)
  NS_INTERFACE_MAP_ENTRY(nsIXPCComponents_Interfaces)
  NS_INTERFACE_MAP_ENTRY(nsIXPCScriptable)
  NS_INTERFACE_MAP_ENTRY(nsIClassInfo)
  NS_INTERFACE_MAP_ENTRY(nsISecurityCheckedComponent)
  NS_INTERFACE_MAP_ENTRY_AMBIGUOUS(nsISupports, nsIXPCComponents_Interfaces)
NS_INTERFACE_MAP_END_THREADSAFE

NS_IMPL_THREADSAFE_ADDREF(nsXPCComponents_Interfaces)
NS_IMPL_THREADSAFE_RELEASE(nsXPCComponents_Interfaces)

// The nsIXPCScriptable map declaration that will generate stubs for us...
#define XPC_MAP_CLASSNAME           nsXPCComponents_Interfaces
#define XPC_MAP_QUOTED_CLASSNAME   "nsXPCComponents_Interfaces"
#define                             XPC_MAP_WANT_NEWRESOLVE
#define                             XPC_MAP_WANT_NEWENUMERATE
#define XPC_MAP_FLAGS               nsIXPCScriptable::DONT_ENUM_STATIC_PROPS |\
                                    nsIXPCScriptable::ALLOW_PROP_MODS_DURING_RESOLVE
#include "xpc_map_end.h" /* This will #undef the above */


/* bool newEnumerate (in nsIXPConnectWrappedNative wrapper, in JSContextPtr cx, in JSObjectPtr obj, in PRUint32 enum_op, in JSValPtr statep, out JSID idp); */
NS_IMETHODIMP
nsXPCComponents_Interfaces::NewEnumerate(nsIXPConnectWrappedNative *wrapper,
                                         JSContext * cx, JSObject * obj,
                                         PRUint32 enum_op, jsval * statep,
                                         jsid * idp, bool *_retval)
{
    nsIEnumerator* e;

    switch (enum_op) {
        case JSENUMERATE_INIT:
        case JSENUMERATE_INIT_ALL:
        {
            if (!mManager ||
                NS_FAILED(mManager->EnumerateInterfaces(&e)) || !e ||
                NS_FAILED(e->First()))

            {
                *statep = JSVAL_NULL;
                return NS_ERROR_UNEXPECTED;
            }

            *statep = PRIVATE_TO_JSVAL(e);
            if (idp)
                *idp = INT_TO_JSID(0); // indicate that we don't know the count
            return NS_OK;
        }
        case JSENUMERATE_NEXT:
        {
            nsCOMPtr<nsISupports> isup;

            e = (nsIEnumerator*) JSVAL_TO_PRIVATE(*statep);

            while (1) {
                if (NS_ENUMERATOR_FALSE == e->IsDone() &&
                    NS_SUCCEEDED(e->CurrentItem(getter_AddRefs(isup))) && isup) {
                    e->Next();
                    nsCOMPtr<nsIInterfaceInfo> iface(do_QueryInterface(isup));
                    if (iface) {
                        JSString* idstr;
                        const char* name;
                        bool scriptable;

                        if (NS_SUCCEEDED(iface->IsScriptable(&scriptable)) &&
                            !scriptable) {
                            continue;
                        }

                        if (NS_SUCCEEDED(iface->GetNameShared(&name)) && name &&
                            nsnull != (idstr = JS_NewStringCopyZ(cx, name)) &&
                            JS_ValueToId(cx, STRING_TO_JSVAL(idstr), idp)) {
                            return NS_OK;
                        }
                    }
                }
                // else...
                break;
            }
            // FALL THROUGH
        }

        case JSENUMERATE_DESTROY:
        default:
            e = (nsIEnumerator*) JSVAL_TO_PRIVATE(*statep);
            NS_IF_RELEASE(e);
            *statep = JSVAL_NULL;
            return NS_OK;
    }
}

/* bool newResolve (in nsIXPConnectWrappedNative wrapper, in JSContextPtr cx, in JSObjectPtr obj, in jsval id, in PRUint32 flags, out JSObjectPtr objp); */
NS_IMETHODIMP
nsXPCComponents_Interfaces::NewResolve(nsIXPConnectWrappedNative *wrapper,
                                       JSContext * cx, JSObject * obj,
                                       jsid id, PRUint32 flags,
                                       JSObject * *objp, bool *_retval)
{
    JSAutoByteString name;
    if (mManager &&
        JSID_IS_STRING(id) &&
        name.encode(cx, JSID_TO_STRING(id)) &&
        name.ptr()[0] != '{') { // we only allow interfaces by name here
        nsCOMPtr<nsIInterfaceInfo> info;
        mManager->GetInfoForName(name.ptr(), getter_AddRefs(info));
        if (!info)
            return NS_OK;

        nsCOMPtr<nsIJSIID> nsid =
            dont_AddRef(static_cast<nsIJSIID*>(nsJSIID::NewID(info)));

        if (nsid) {
            nsCOMPtr<nsIXPConnect> xpc;
            wrapper->GetXPConnect(getter_AddRefs(xpc));
            if (xpc) {
                nsCOMPtr<nsIXPConnectJSObjectHolder> holder;
                if (NS_SUCCEEDED(xpc->WrapNative(cx, obj,
                                                 static_cast<nsIJSIID*>(nsid),
                                                 NS_GET_IID(nsIJSIID),
                                                 getter_AddRefs(holder)))) {
                    JSObject* idobj;
                    if (holder && NS_SUCCEEDED(holder->GetJSObject(&idobj))) {
                        *objp = obj;
                        *_retval = JS_DefinePropertyById(cx, obj, id,
                                                         OBJECT_TO_JSVAL(idobj),
                                                         nsnull, nsnull,
                                                         JSPROP_ENUMERATE |
                                                         JSPROP_READONLY |
                                                         JSPROP_PERMANENT);
                    }
                }
            }
        }
    }
    return NS_OK;
}

/* string canCreateWrapper (in nsIIDPtr iid); */
NS_IMETHODIMP
nsXPCComponents_Interfaces::CanCreateWrapper(const nsIID * iid, char **_retval)
{
    // We let anyone do this...
    *_retval = xpc_CloneAllAccess();
    return NS_OK;
}

/* string canCallMethod (in nsIIDPtr iid, in wstring methodName); */
NS_IMETHODIMP
nsXPCComponents_Interfaces::CanCallMethod(const nsIID * iid, const PRUnichar *methodName, char **_retval)
{
    // If you have to ask, then the answer is NO
    *_retval = nsnull;
    return NS_OK;
}

/* string canGetProperty (in nsIIDPtr iid, in wstring propertyName); */
NS_IMETHODIMP
nsXPCComponents_Interfaces::CanGetProperty(const nsIID * iid, const PRUnichar *propertyName, char **_retval)
{
    // If you have to ask, then the answer is NO
    *_retval = nsnull;
    return NS_OK;
}

/* string canSetProperty (in nsIIDPtr iid, in wstring propertyName); */
NS_IMETHODIMP
nsXPCComponents_Interfaces::CanSetProperty(const nsIID * iid, const PRUnichar *propertyName, char **_retval)
{
    // If you have to ask, then the answer is NO
    *_retval = nsnull;
    return NS_OK;
}

/***************************************************************************/
/***************************************************************************/
/***************************************************************************/

class nsXPCComponents_InterfacesByID :
            public nsIXPCComponents_InterfacesByID,
            public nsIXPCScriptable,
            public nsIClassInfo,
            public nsISecurityCheckedComponent
{
public:
    // all the interface method declarations...
    NS_DECL_ISUPPORTS
    NS_DECL_NSIXPCCOMPONENTS_INTERFACESBYID
    NS_DECL_NSIXPCSCRIPTABLE
    NS_DECL_NSICLASSINFO
    NS_DECL_NSISECURITYCHECKEDCOMPONENT

public:
    nsXPCComponents_InterfacesByID();
    virtual ~nsXPCComponents_InterfacesByID();

private:
    nsCOMPtr<nsIInterfaceInfoManager> mManager;
};

/***************************************************************************/
/* void getInterfaces (out PRUint32 count, [array, size_is (count), retval]
                       out nsIIDPtr array); */
NS_IMETHODIMP
nsXPCComponents_InterfacesByID::GetInterfaces(PRUint32 *aCount, nsIID * **aArray)
{
    const PRUint32 count = 3;
    *aCount = count;
    nsIID **array;
    *aArray = array = static_cast<nsIID**>(nsMemory::Alloc(count * sizeof(nsIID*)));
    if (!array)
        return NS_ERROR_OUT_OF_MEMORY;

    PRUint32 index = 0;
    nsIID* clone;
#define PUSH_IID(id)                                                          \
    clone = static_cast<nsIID *>(nsMemory::Clone(&NS_GET_IID( id ),           \
                                                 sizeof(nsIID)));             \
    if (!clone)                                                               \
        goto oom;                                                             \
    array[index++] = clone;

    PUSH_IID(nsIXPCComponents_InterfacesByID)
    PUSH_IID(nsIXPCScriptable)
    PUSH_IID(nsISecurityCheckedComponent)
#undef PUSH_IID

    return NS_OK;
oom:
    while (index)
        nsMemory::Free(array[--index]);
    nsMemory::Free(array);
    *aArray = nsnull;
    return NS_ERROR_OUT_OF_MEMORY;
}

/* nsISupports getHelperForLanguage (in PRUint32 language); */
NS_IMETHODIMP
nsXPCComponents_InterfacesByID::GetHelperForLanguage(PRUint32 language,
                                                     nsISupports **retval)
{
    *retval = nsnull;
    return NS_OK;
}

/* readonly attribute string contractID; */
NS_IMETHODIMP
nsXPCComponents_InterfacesByID::GetContractID(char * *aContractID)
{
    *aContractID = nsnull;
    return NS_ERROR_NOT_AVAILABLE;
}

/* readonly attribute string classDescription; */
NS_IMETHODIMP
nsXPCComponents_InterfacesByID::GetClassDescription(char * *aClassDescription)
{
    static const char classDescription[] = "XPCComponents_InterfacesByID";
    *aClassDescription = (char*)nsMemory::Clone(classDescription, sizeof(classDescription));
    return *aClassDescription ? NS_OK : NS_ERROR_OUT_OF_MEMORY;
}

/* readonly attribute nsCIDPtr classID; */
NS_IMETHODIMP
nsXPCComponents_InterfacesByID::GetClassID(nsCID * *aClassID)
{
    *aClassID = nsnull;
    return NS_OK;
}

/* readonly attribute PRUint32 implementationLanguage; */
NS_IMETHODIMP
nsXPCComponents_InterfacesByID::GetImplementationLanguage(PRUint32 *aImplementationLanguage)
{
    *aImplementationLanguage = nsIProgrammingLanguage::CPLUSPLUS;
    return NS_OK;
}

/* readonly attribute PRUint32 flags; */
NS_IMETHODIMP
nsXPCComponents_InterfacesByID::GetFlags(PRUint32 *aFlags)
{
    *aFlags = nsIClassInfo::THREADSAFE;
    return NS_OK;
}

/* [notxpcom] readonly attribute nsCID classIDNoAlloc; */
NS_IMETHODIMP
nsXPCComponents_InterfacesByID::GetClassIDNoAlloc(nsCID *aClassIDNoAlloc)
{
    return NS_ERROR_NOT_AVAILABLE;
}

nsXPCComponents_InterfacesByID::nsXPCComponents_InterfacesByID() :
    mManager(do_GetService(NS_INTERFACEINFOMANAGER_SERVICE_CONTRACTID))
{
}

nsXPCComponents_InterfacesByID::~nsXPCComponents_InterfacesByID()
{
    // empty
}

NS_INTERFACE_MAP_BEGIN(nsXPCComponents_InterfacesByID)
  NS_INTERFACE_MAP_ENTRY(nsIXPCComponents_InterfacesByID)
  NS_INTERFACE_MAP_ENTRY(nsIXPCScriptable)
  NS_INTERFACE_MAP_ENTRY(nsIClassInfo)
  NS_INTERFACE_MAP_ENTRY(nsISecurityCheckedComponent)
  NS_INTERFACE_MAP_ENTRY_AMBIGUOUS(nsISupports, nsIXPCComponents_InterfacesByID)
NS_INTERFACE_MAP_END_THREADSAFE

NS_IMPL_THREADSAFE_ADDREF(nsXPCComponents_InterfacesByID)
NS_IMPL_THREADSAFE_RELEASE(nsXPCComponents_InterfacesByID)

// The nsIXPCScriptable map declaration that will generate stubs for us...
#define XPC_MAP_CLASSNAME           nsXPCComponents_InterfacesByID
#define XPC_MAP_QUOTED_CLASSNAME   "nsXPCComponents_InterfacesByID"
#define                             XPC_MAP_WANT_NEWRESOLVE
#define                             XPC_MAP_WANT_NEWENUMERATE
#define XPC_MAP_FLAGS               nsIXPCScriptable::DONT_ENUM_STATIC_PROPS |\
                                    nsIXPCScriptable::ALLOW_PROP_MODS_DURING_RESOLVE
#include "xpc_map_end.h" /* This will #undef the above */

/* bool newEnumerate (in nsIXPConnectWrappedNative wrapper, in JSContextPtr cx, in JSObjectPtr obj, in PRUint32 enum_op, in JSValPtr statep, out JSID idp); */
NS_IMETHODIMP
nsXPCComponents_InterfacesByID::NewEnumerate(nsIXPConnectWrappedNative *wrapper,
                                             JSContext * cx, JSObject * obj,
                                             PRUint32 enum_op, jsval * statep,
                                             jsid * idp, bool *_retval)
{
    nsIEnumerator* e;

    switch (enum_op) {
        case JSENUMERATE_INIT:
        case JSENUMERATE_INIT_ALL:
        {
            if (!mManager ||
                NS_FAILED(mManager->EnumerateInterfaces(&e)) || !e ||
                NS_FAILED(e->First()))

            {
                *statep = JSVAL_NULL;
                return NS_ERROR_UNEXPECTED;
            }

            *statep = PRIVATE_TO_JSVAL(e);
            if (idp)
                *idp = INT_TO_JSID(0); // indicate that we don't know the count
            return NS_OK;
        }
        case JSENUMERATE_NEXT:
        {
            nsCOMPtr<nsISupports> isup;

            e = (nsIEnumerator*) JSVAL_TO_PRIVATE(*statep);

            while (1) {
                if (NS_ENUMERATOR_FALSE == e->IsDone() &&
                    NS_SUCCEEDED(e->CurrentItem(getter_AddRefs(isup))) && isup) {
                    e->Next();
                    nsCOMPtr<nsIInterfaceInfo> iface(do_QueryInterface(isup));
                    if (iface) {
                        nsIID const *iid;
                        char idstr[NSID_LENGTH];
                        JSString* jsstr;
                        bool scriptable;

                        if (NS_SUCCEEDED(iface->IsScriptable(&scriptable)) &&
                            !scriptable) {
                            continue;
                        }

                        if (NS_SUCCEEDED(iface->GetIIDShared(&iid))) {
                            iid->ToProvidedString(idstr);
                            jsstr = JS_NewStringCopyZ(cx, idstr);
                            if (jsstr &&
                                JS_ValueToId(cx, STRING_TO_JSVAL(jsstr), idp)) {
                                return NS_OK;
                            }
                        }
                    }
                }
                // else...
                break;
            }
            // FALL THROUGH
        }

        case JSENUMERATE_DESTROY:
        default:
            e = (nsIEnumerator*) JSVAL_TO_PRIVATE(*statep);
            NS_IF_RELEASE(e);
            *statep = JSVAL_NULL;
            return NS_OK;
    }
}

/* bool newResolve (in nsIXPConnectWrappedNative wrapper, in JSContextPtr cx, in JSObjectPtr obj, in jsval id, in PRUint32 flags, out JSObjectPtr objp); */
NS_IMETHODIMP
nsXPCComponents_InterfacesByID::NewResolve(nsIXPConnectWrappedNative *wrapper,
                                           JSContext * cx, JSObject * obj,
                                           jsid id, PRUint32 flags,
                                           JSObject * *objp, bool *_retval)
{
    const jschar* name = nsnull;

    if (mManager &&
        JSID_IS_STRING(id) &&
        38 == JS_GetStringLength(JSID_TO_STRING(id)) &&
        nsnull != (name = JS_GetInternedStringChars(JSID_TO_STRING(id)))) {
        nsID iid;
        if (!iid.Parse(NS_ConvertUTF16toUTF8(name).get()))
            return NS_OK;

        nsCOMPtr<nsIInterfaceInfo> info;
        mManager->GetInfoForIID(&iid, getter_AddRefs(info));
        if (!info)
            return NS_OK;

        nsCOMPtr<nsIJSIID> nsid =
            dont_AddRef(static_cast<nsIJSIID*>(nsJSIID::NewID(info)));

        if (!nsid)
            return NS_ERROR_OUT_OF_MEMORY;

        nsCOMPtr<nsIXPConnect> xpc;
        wrapper->GetXPConnect(getter_AddRefs(xpc));
        if (xpc) {
            nsCOMPtr<nsIXPConnectJSObjectHolder> holder;
            if (NS_SUCCEEDED(xpc->WrapNative(cx, obj,
                                             static_cast<nsIJSIID*>(nsid),
                                             NS_GET_IID(nsIJSIID),
                                             getter_AddRefs(holder)))) {
                JSObject* idobj;
                if (holder && NS_SUCCEEDED(holder->GetJSObject(&idobj))) {
                    *objp = obj;
                    *_retval =
                        JS_DefinePropertyById(cx, obj, id,
                                              OBJECT_TO_JSVAL(idobj),
                                              nsnull, nsnull,
                                              JSPROP_ENUMERATE |
                                              JSPROP_READONLY |
                                              JSPROP_PERMANENT);
                }
            }
        }
    }
    return NS_OK;
}

/* string canCreateWrapper (in nsIIDPtr iid); */
NS_IMETHODIMP
nsXPCComponents_InterfacesByID::CanCreateWrapper(const nsIID * iid, char **_retval)
{
    // We let anyone do this...
    *_retval = xpc_CloneAllAccess();
    return NS_OK;
}

/* string canCallMethod (in nsIIDPtr iid, in wstring methodName); */
NS_IMETHODIMP
nsXPCComponents_InterfacesByID::CanCallMethod(const nsIID * iid, const PRUnichar *methodName, char **_retval)
{
    // If you have to ask, then the answer is NO
    *_retval = nsnull;
    return NS_OK;
}

/* string canGetProperty (in nsIIDPtr iid, in wstring propertyName); */
NS_IMETHODIMP
nsXPCComponents_InterfacesByID::CanGetProperty(const nsIID * iid, const PRUnichar *propertyName, char **_retval)
{
    // If you have to ask, then the answer is NO
    *_retval = nsnull;
    return NS_OK;
}

/* string canSetProperty (in nsIIDPtr iid, in wstring propertyName); */
NS_IMETHODIMP
nsXPCComponents_InterfacesByID::CanSetProperty(const nsIID * iid, const PRUnichar *propertyName, char **_retval)
{
    // If you have to ask, then the answer is NO
    *_retval = nsnull;
    return NS_OK;
}

/***************************************************************************/
/***************************************************************************/
/***************************************************************************/



class nsXPCComponents_Classes :
  public nsIXPCComponents_Classes,
  public nsIXPCScriptable,
  public nsIClassInfo
{
public:
    // all the interface method declarations...
    NS_DECL_ISUPPORTS
    NS_DECL_NSIXPCCOMPONENTS_CLASSES
    NS_DECL_NSIXPCSCRIPTABLE
    NS_DECL_NSICLASSINFO

public:
    nsXPCComponents_Classes();
    virtual ~nsXPCComponents_Classes();
};

/***************************************************************************/
/* void getInterfaces (out PRUint32 count, [array, size_is (count), retval]
                       out nsIIDPtr array); */
NS_IMETHODIMP
nsXPCComponents_Classes::GetInterfaces(PRUint32 *aCount, nsIID * **aArray)
{
    const PRUint32 count = 2;
    *aCount = count;
    nsIID **array;
    *aArray = array = static_cast<nsIID**>(nsMemory::Alloc(count * sizeof(nsIID*)));
    if (!array)
        return NS_ERROR_OUT_OF_MEMORY;

    PRUint32 index = 0;
    nsIID* clone;
#define PUSH_IID(id)                                                          \
    clone = static_cast<nsIID *>(nsMemory::Clone(&NS_GET_IID( id ),           \
                                                 sizeof(nsIID)));             \
    if (!clone)                                                               \
        goto oom;                                                             \
    array[index++] = clone;

    PUSH_IID(nsIXPCComponents_Classes)
    PUSH_IID(nsIXPCScriptable)
#undef PUSH_IID

    return NS_OK;
oom:
    while (index)
        nsMemory::Free(array[--index]);
    nsMemory::Free(array);
    *aArray = nsnull;
    return NS_ERROR_OUT_OF_MEMORY;
}

/* nsISupports getHelperForLanguage (in PRUint32 language); */
NS_IMETHODIMP
nsXPCComponents_Classes::GetHelperForLanguage(PRUint32 language,
                                              nsISupports **retval)
{
    *retval = nsnull;
    return NS_OK;
}

/* readonly attribute string contractID; */
NS_IMETHODIMP
nsXPCComponents_Classes::GetContractID(char * *aContractID)
{
    *aContractID = nsnull;
    return NS_ERROR_NOT_AVAILABLE;
}

/* readonly attribute string classDescription; */
NS_IMETHODIMP
nsXPCComponents_Classes::GetClassDescription(char * *aClassDescription)
{
    static const char classDescription[] = "XPCComponents_Classes";
    *aClassDescription = (char*)nsMemory::Clone(classDescription, sizeof(classDescription));
    return *aClassDescription ? NS_OK : NS_ERROR_OUT_OF_MEMORY;
}

/* readonly attribute nsCIDPtr classID; */
NS_IMETHODIMP
nsXPCComponents_Classes::GetClassID(nsCID * *aClassID)
{
    *aClassID = nsnull;
    return NS_OK;
}

/* readonly attribute PRUint32 implementationLanguage; */
NS_IMETHODIMP
nsXPCComponents_Classes::GetImplementationLanguage(PRUint32 *aImplementationLanguage)
{
    *aImplementationLanguage = nsIProgrammingLanguage::CPLUSPLUS;
    return NS_OK;
}

/* readonly attribute PRUint32 flags; */
NS_IMETHODIMP
nsXPCComponents_Classes::GetFlags(PRUint32 *aFlags)
{
    *aFlags = nsIClassInfo::THREADSAFE;
    return NS_OK;
}

/* [notxpcom] readonly attribute nsCID classIDNoAlloc; */
NS_IMETHODIMP
nsXPCComponents_Classes::GetClassIDNoAlloc(nsCID *aClassIDNoAlloc)
{
    return NS_ERROR_NOT_AVAILABLE;
}

nsXPCComponents_Classes::nsXPCComponents_Classes()
{
}

nsXPCComponents_Classes::~nsXPCComponents_Classes()
{
    // empty
}

NS_INTERFACE_MAP_BEGIN(nsXPCComponents_Classes)
  NS_INTERFACE_MAP_ENTRY(nsIXPCComponents_Classes)
  NS_INTERFACE_MAP_ENTRY(nsIXPCScriptable)
  NS_INTERFACE_MAP_ENTRY(nsIClassInfo)
  NS_INTERFACE_MAP_ENTRY_AMBIGUOUS(nsISupports, nsIXPCComponents_Classes)
NS_INTERFACE_MAP_END_THREADSAFE

NS_IMPL_THREADSAFE_ADDREF(nsXPCComponents_Classes)
NS_IMPL_THREADSAFE_RELEASE(nsXPCComponents_Classes)

// The nsIXPCScriptable map declaration that will generate stubs for us...
#define XPC_MAP_CLASSNAME           nsXPCComponents_Classes
#define XPC_MAP_QUOTED_CLASSNAME   "nsXPCComponents_Classes"
#define                             XPC_MAP_WANT_NEWRESOLVE
#define                             XPC_MAP_WANT_NEWENUMERATE
#define XPC_MAP_FLAGS               nsIXPCScriptable::DONT_ENUM_STATIC_PROPS |\
                                    nsIXPCScriptable::ALLOW_PROP_MODS_DURING_RESOLVE
#include "xpc_map_end.h" /* This will #undef the above */


/* bool newEnumerate (in nsIXPConnectWrappedNative wrapper, in JSContextPtr cx, in JSObjectPtr obj, in PRUint32 enum_op, in JSValPtr statep, out JSID idp); */
NS_IMETHODIMP
nsXPCComponents_Classes::NewEnumerate(nsIXPConnectWrappedNative *wrapper,
                                      JSContext * cx, JSObject * obj,
                                      PRUint32 enum_op, jsval * statep,
                                      jsid * idp, bool *_retval)
{
    nsISimpleEnumerator* e;

    switch (enum_op) {
        case JSENUMERATE_INIT:
        case JSENUMERATE_INIT_ALL:
        {
            nsCOMPtr<nsIComponentRegistrar> compMgr;
            if (NS_FAILED(NS_GetComponentRegistrar(getter_AddRefs(compMgr))) || !compMgr ||
                NS_FAILED(compMgr->EnumerateContractIDs(&e)) || !e ) {
                *statep = JSVAL_NULL;
                return NS_ERROR_UNEXPECTED;
            }

            *statep = PRIVATE_TO_JSVAL(e);
            if (idp)
                *idp = INT_TO_JSID(0); // indicate that we don't know the count
            return NS_OK;
        }
        case JSENUMERATE_NEXT:
        {
            nsCOMPtr<nsISupports> isup;
            bool hasMore;
            e = (nsISimpleEnumerator*) JSVAL_TO_PRIVATE(*statep);

            if (NS_SUCCEEDED(e->HasMoreElements(&hasMore)) && hasMore &&
                NS_SUCCEEDED(e->GetNext(getter_AddRefs(isup))) && isup) {
                nsCOMPtr<nsISupportsCString> holder(do_QueryInterface(isup));
                if (holder) {
                    nsCAutoString name;
                    if (NS_SUCCEEDED(holder->GetData(name))) {
                        JSString* idstr = JS_NewStringCopyN(cx, name.get(), name.Length());
                        if (idstr &&
                            JS_ValueToId(cx, STRING_TO_JSVAL(idstr), idp)) {
                            return NS_OK;
                        }
                    }
                }
            }
            // else... FALL THROUGH
        }

        case JSENUMERATE_DESTROY:
        default:
            e = (nsISimpleEnumerator*) JSVAL_TO_PRIVATE(*statep);
            NS_IF_RELEASE(e);
            *statep = JSVAL_NULL;
            return NS_OK;
    }
}

/* bool newResolve (in nsIXPConnectWrappedNative wrapper, in JSContextPtr cx, in JSObjectPtr obj, in jsval id, in PRUint32 flags, out JSObjectPtr objp); */
NS_IMETHODIMP
nsXPCComponents_Classes::NewResolve(nsIXPConnectWrappedNative *wrapper,
                                    JSContext * cx, JSObject * obj,
                                    jsid id, PRUint32 flags,
                                    JSObject * *objp, bool *_retval)

{
    JSAutoByteString name;

    if (JSID_IS_STRING(id) &&
        name.encode(cx, JSID_TO_STRING(id)) &&
        name.ptr()[0] != '{') { // we only allow contractids here
        nsCOMPtr<nsIJSCID> nsid =
            dont_AddRef(static_cast<nsIJSCID*>(nsJSCID::NewID(name.ptr())));
        if (nsid) {
            nsCOMPtr<nsIXPConnect> xpc;
            wrapper->GetXPConnect(getter_AddRefs(xpc));
            if (xpc) {
                nsCOMPtr<nsIXPConnectJSObjectHolder> holder;
                if (NS_SUCCEEDED(xpc->WrapNative(cx, obj,
                                                 static_cast<nsIJSCID*>(nsid),
                                                 NS_GET_IID(nsIJSCID),
                                                 getter_AddRefs(holder)))) {
                    JSObject* idobj;
                    if (holder && NS_SUCCEEDED(holder->GetJSObject(&idobj))) {
                        *objp = obj;
                        *_retval = JS_DefinePropertyById(cx, obj, id,
                                                         OBJECT_TO_JSVAL(idobj),
                                                         nsnull, nsnull,
                                                         JSPROP_ENUMERATE |
                                                         JSPROP_READONLY |
                                                         JSPROP_PERMANENT);
                    }
                }
            }
        }
    }
    return NS_OK;
}

/***************************************************************************/
/***************************************************************************/
/***************************************************************************/

class nsXPCComponents_ClassesByID :
  public nsIXPCComponents_ClassesByID,
  public nsIXPCScriptable,
  public nsIClassInfo
{
public:
    // all the interface method declarations...
    NS_DECL_ISUPPORTS
    NS_DECL_NSIXPCCOMPONENTS_CLASSESBYID
    NS_DECL_NSIXPCSCRIPTABLE
    NS_DECL_NSICLASSINFO

public:
    nsXPCComponents_ClassesByID();
    virtual ~nsXPCComponents_ClassesByID();
};

/***************************************************************************/
/* void getInterfaces (out PRUint32 count, [array, size_is (count), retval]
                       out nsIIDPtr array); */
NS_IMETHODIMP
nsXPCComponents_ClassesByID::GetInterfaces(PRUint32 *aCount, nsIID * **aArray)
{
    const PRUint32 count = 2;
    *aCount = count;
    nsIID **array;
    *aArray = array = static_cast<nsIID**>(nsMemory::Alloc(count * sizeof(nsIID*)));
    if (!array)
        return NS_ERROR_OUT_OF_MEMORY;

    PRUint32 index = 0;
    nsIID* clone;
#define PUSH_IID(id)                                                          \
    clone = static_cast<nsIID *>(nsMemory::Clone(&NS_GET_IID( id ),           \
                                                 sizeof(nsIID)));             \
    if (!clone)                                                               \
        goto oom;                                                             \
    array[index++] = clone;

    PUSH_IID(nsIXPCComponents_ClassesByID)
    PUSH_IID(nsIXPCScriptable)
#undef PUSH_IID

    return NS_OK;
oom:
    while (index)
        nsMemory::Free(array[--index]);
    nsMemory::Free(array);
    *aArray = nsnull;
    return NS_ERROR_OUT_OF_MEMORY;
}

/* nsISupports getHelperForLanguage (in PRUint32 language); */
NS_IMETHODIMP
nsXPCComponents_ClassesByID::GetHelperForLanguage(PRUint32 language,
                                                  nsISupports **retval)
{
    *retval = nsnull;
    return NS_OK;
}

/* readonly attribute string contractID; */
NS_IMETHODIMP
nsXPCComponents_ClassesByID::GetContractID(char * *aContractID)
{
    *aContractID = nsnull;
    return NS_ERROR_NOT_AVAILABLE;
}

/* readonly attribute string classDescription; */
NS_IMETHODIMP
nsXPCComponents_ClassesByID::GetClassDescription(char * *aClassDescription)
{
    static const char classDescription[] = "XPCComponents_ClassesByID";
    *aClassDescription = (char*)nsMemory::Clone(classDescription, sizeof(classDescription));
    return *aClassDescription ? NS_OK : NS_ERROR_OUT_OF_MEMORY;
}

/* readonly attribute nsCIDPtr classID; */
NS_IMETHODIMP
nsXPCComponents_ClassesByID::GetClassID(nsCID * *aClassID)
{
    *aClassID = nsnull;
    return NS_OK;
}

/* readonly attribute PRUint32 implementationLanguage; */
NS_IMETHODIMP
nsXPCComponents_ClassesByID::GetImplementationLanguage(PRUint32 *aImplementationLanguage)
{
    *aImplementationLanguage = nsIProgrammingLanguage::CPLUSPLUS;
    return NS_OK;
}

/* readonly attribute PRUint32 flags; */
NS_IMETHODIMP
nsXPCComponents_ClassesByID::GetFlags(PRUint32 *aFlags)
{
    *aFlags = nsIClassInfo::THREADSAFE;
    return NS_OK;
}

/* [notxpcom] readonly attribute nsCID classIDNoAlloc; */
NS_IMETHODIMP
nsXPCComponents_ClassesByID::GetClassIDNoAlloc(nsCID *aClassIDNoAlloc)
{
    return NS_ERROR_NOT_AVAILABLE;
}

nsXPCComponents_ClassesByID::nsXPCComponents_ClassesByID()
{
}

nsXPCComponents_ClassesByID::~nsXPCComponents_ClassesByID()
{
    // empty
}

NS_INTERFACE_MAP_BEGIN(nsXPCComponents_ClassesByID)
  NS_INTERFACE_MAP_ENTRY(nsIXPCComponents_ClassesByID)
  NS_INTERFACE_MAP_ENTRY(nsIXPCScriptable)
  NS_INTERFACE_MAP_ENTRY(nsIClassInfo)
  NS_INTERFACE_MAP_ENTRY_AMBIGUOUS(nsISupports, nsIXPCComponents_ClassesByID)
NS_INTERFACE_MAP_END_THREADSAFE

NS_IMPL_THREADSAFE_ADDREF(nsXPCComponents_ClassesByID)
NS_IMPL_THREADSAFE_RELEASE(nsXPCComponents_ClassesByID)

// The nsIXPCScriptable map declaration that will generate stubs for us...
#define XPC_MAP_CLASSNAME           nsXPCComponents_ClassesByID
#define XPC_MAP_QUOTED_CLASSNAME   "nsXPCComponents_ClassesByID"
#define                             XPC_MAP_WANT_NEWRESOLVE
#define                             XPC_MAP_WANT_NEWENUMERATE
#define XPC_MAP_FLAGS               nsIXPCScriptable::DONT_ENUM_STATIC_PROPS |\
                                    nsIXPCScriptable::ALLOW_PROP_MODS_DURING_RESOLVE
#include "xpc_map_end.h" /* This will #undef the above */

/* bool newEnumerate (in nsIXPConnectWrappedNative wrapper, in JSContextPtr cx, in JSObjectPtr obj, in PRUint32 enum_op, in JSValPtr statep, out JSID idp); */
NS_IMETHODIMP
nsXPCComponents_ClassesByID::NewEnumerate(nsIXPConnectWrappedNative *wrapper,
                                          JSContext * cx, JSObject * obj,
                                          PRUint32 enum_op, jsval * statep,
                                          jsid * idp, bool *_retval)
{
    nsISimpleEnumerator* e;

    switch (enum_op) {
        case JSENUMERATE_INIT:
        case JSENUMERATE_INIT_ALL:
        {
            nsCOMPtr<nsIComponentRegistrar> compMgr;
            if (NS_FAILED(NS_GetComponentRegistrar(getter_AddRefs(compMgr))) || !compMgr ||
                NS_FAILED(compMgr->EnumerateCIDs(&e)) || !e ) {
                *statep = JSVAL_NULL;
                return NS_ERROR_UNEXPECTED;
            }

            *statep = PRIVATE_TO_JSVAL(e);
            if (idp)
                *idp = INT_TO_JSID(0); // indicate that we don't know the count
            return NS_OK;
        }
        case JSENUMERATE_NEXT:
        {
            nsCOMPtr<nsISupports> isup;
            bool hasMore;
            e = (nsISimpleEnumerator*) JSVAL_TO_PRIVATE(*statep);

            if (NS_SUCCEEDED(e->HasMoreElements(&hasMore)) && hasMore &&
                NS_SUCCEEDED(e->GetNext(getter_AddRefs(isup))) && isup) {
                nsCOMPtr<nsISupportsID> holder(do_QueryInterface(isup));
                if (holder) {
                    char* name;
                    if (NS_SUCCEEDED(holder->ToString(&name)) && name) {
                        JSString* idstr = JS_NewStringCopyZ(cx, name);
                        nsMemory::Free(name);
                        if (idstr &&
                            JS_ValueToId(cx, STRING_TO_JSVAL(idstr), idp)) {
                            return NS_OK;
                        }
                    }
                }
            }
            // else... FALL THROUGH
        }

        case JSENUMERATE_DESTROY:
        default:
            e = (nsISimpleEnumerator*) JSVAL_TO_PRIVATE(*statep);
            NS_IF_RELEASE(e);
            *statep = JSVAL_NULL;
            return NS_OK;
    }
}

static bool
IsRegisteredCLSID(const char* str)
{
    bool registered;
    nsID id;

    if (!id.Parse(str))
        return false;

    nsCOMPtr<nsIComponentRegistrar> compMgr;
    if (NS_FAILED(NS_GetComponentRegistrar(getter_AddRefs(compMgr))) || !compMgr ||
        NS_FAILED(compMgr->IsCIDRegistered(id, &registered)))
        return false;

    return registered;
}

/* bool newResolve (in nsIXPConnectWrappedNative wrapper, in JSContextPtr cx, in JSObjectPtr obj, in jsval id, in PRUint32 flags, out JSObjectPtr objp); */
NS_IMETHODIMP
nsXPCComponents_ClassesByID::NewResolve(nsIXPConnectWrappedNative *wrapper,
                                        JSContext * cx, JSObject * obj,
                                        jsid id, PRUint32 flags,
                                        JSObject * *objp, bool *_retval)
{
    JSAutoByteString name;

    if (JSID_IS_STRING(id) &&
        name.encode(cx, JSID_TO_STRING(id)) &&
        name.ptr()[0] == '{' &&
        IsRegisteredCLSID(name.ptr())) { // we only allow canonical CLSIDs here
        nsCOMPtr<nsIJSCID> nsid =
            dont_AddRef(static_cast<nsIJSCID*>(nsJSCID::NewID(name.ptr())));
        if (nsid) {
            nsCOMPtr<nsIXPConnect> xpc;
            wrapper->GetXPConnect(getter_AddRefs(xpc));
            if (xpc) {
                nsCOMPtr<nsIXPConnectJSObjectHolder> holder;
                if (NS_SUCCEEDED(xpc->WrapNative(cx, obj,
                                                 static_cast<nsIJSCID*>(nsid),
                                                 NS_GET_IID(nsIJSCID),
                                                 getter_AddRefs(holder)))) {
                    JSObject* idobj;
                    if (holder && NS_SUCCEEDED(holder->GetJSObject(&idobj))) {
                        *objp = obj;
                        *_retval = JS_DefinePropertyById(cx, obj, id,
                                                         OBJECT_TO_JSVAL(idobj),
                                                         nsnull, nsnull,
                                                         JSPROP_ENUMERATE |
                                                         JSPROP_READONLY |
                                                         JSPROP_PERMANENT);
                    }
                }
            }
        }
    }
    return NS_OK;
}


/***************************************************************************/

// Currently the possible results do not change at runtime, so they are only
// cached once (unlike ContractIDs, CLSIDs, and IIDs)

class nsXPCComponents_Results :
  public nsIXPCComponents_Results,
  public nsIXPCScriptable,
  public nsIClassInfo
{
public:
    // all the interface method declarations...
    NS_DECL_ISUPPORTS
    NS_DECL_NSIXPCCOMPONENTS_RESULTS
    NS_DECL_NSIXPCSCRIPTABLE
    NS_DECL_NSICLASSINFO

public:
    nsXPCComponents_Results();
    virtual ~nsXPCComponents_Results();
};

/***************************************************************************/
/* void getInterfaces (out PRUint32 count, [array, size_is (count), retval]
                       out nsIIDPtr array); */
NS_IMETHODIMP
nsXPCComponents_Results::GetInterfaces(PRUint32 *aCount, nsIID * **aArray)
{
    const PRUint32 count = 2;
    *aCount = count;
    nsIID **array;
    *aArray = array = static_cast<nsIID**>(nsMemory::Alloc(count * sizeof(nsIID*)));
    if (!array)
        return NS_ERROR_OUT_OF_MEMORY;

    PRUint32 index = 0;
    nsIID* clone;
#define PUSH_IID(id)                                                          \
    clone = static_cast<nsIID *>(nsMemory::Clone(&NS_GET_IID( id ),           \
                                                 sizeof(nsIID)));             \
    if (!clone)                                                               \
        goto oom;                                                             \
    array[index++] = clone;

    PUSH_IID(nsIXPCComponents_Results)
    PUSH_IID(nsIXPCScriptable)
#undef PUSH_IID

    return NS_OK;
oom:
    while (index)
        nsMemory::Free(array[--index]);
    nsMemory::Free(array);
    *aArray = nsnull;
    return NS_ERROR_OUT_OF_MEMORY;
}

/* nsISupports getHelperForLanguage (in PRUint32 language); */
NS_IMETHODIMP
nsXPCComponents_Results::GetHelperForLanguage(PRUint32 language,
                                              nsISupports **retval)
{
    *retval = nsnull;
    return NS_OK;
}

/* readonly attribute string contractID; */
NS_IMETHODIMP
nsXPCComponents_Results::GetContractID(char * *aContractID)
{
    *aContractID = nsnull;
    return NS_ERROR_NOT_AVAILABLE;
}

/* readonly attribute string classDescription; */
NS_IMETHODIMP
nsXPCComponents_Results::GetClassDescription(char * *aClassDescription)
{
    static const char classDescription[] = "XPCComponents_Results";
    *aClassDescription = (char*)nsMemory::Clone(classDescription, sizeof(classDescription));
    return *aClassDescription ? NS_OK : NS_ERROR_OUT_OF_MEMORY;
}

/* readonly attribute nsCIDPtr classID; */
NS_IMETHODIMP
nsXPCComponents_Results::GetClassID(nsCID * *aClassID)
{
    *aClassID = nsnull;
    return NS_OK;
}

/* readonly attribute PRUint32 implementationLanguage; */
NS_IMETHODIMP
nsXPCComponents_Results::GetImplementationLanguage(PRUint32 *aImplementationLanguage)
{
    *aImplementationLanguage = nsIProgrammingLanguage::CPLUSPLUS;
    return NS_OK;
}

/* readonly attribute PRUint32 flags; */
NS_IMETHODIMP
nsXPCComponents_Results::GetFlags(PRUint32 *aFlags)
{
    *aFlags = nsIClassInfo::THREADSAFE;
    return NS_OK;
}

/* [notxpcom] readonly attribute nsCID classIDNoAlloc; */
NS_IMETHODIMP
nsXPCComponents_Results::GetClassIDNoAlloc(nsCID *aClassIDNoAlloc)
{
    return NS_ERROR_NOT_AVAILABLE;
}

nsXPCComponents_Results::nsXPCComponents_Results()
{
}

nsXPCComponents_Results::~nsXPCComponents_Results()
{
    // empty
}

NS_INTERFACE_MAP_BEGIN(nsXPCComponents_Results)
  NS_INTERFACE_MAP_ENTRY(nsIXPCComponents_Results)
  NS_INTERFACE_MAP_ENTRY(nsIXPCScriptable)
  NS_INTERFACE_MAP_ENTRY(nsIClassInfo)
  NS_INTERFACE_MAP_ENTRY_AMBIGUOUS(nsISupports, nsIXPCComponents_Results)
NS_INTERFACE_MAP_END_THREADSAFE

NS_IMPL_THREADSAFE_ADDREF(nsXPCComponents_Results)
NS_IMPL_THREADSAFE_RELEASE(nsXPCComponents_Results)

// The nsIXPCScriptable map declaration that will generate stubs for us...
#define XPC_MAP_CLASSNAME           nsXPCComponents_Results
#define XPC_MAP_QUOTED_CLASSNAME   "nsXPCComponents_Results"
#define                             XPC_MAP_WANT_NEWRESOLVE
#define                             XPC_MAP_WANT_NEWENUMERATE
#define XPC_MAP_FLAGS               nsIXPCScriptable::DONT_ENUM_STATIC_PROPS |\
                                    nsIXPCScriptable::ALLOW_PROP_MODS_DURING_RESOLVE
#include "xpc_map_end.h" /* This will #undef the above */

/* bool newEnumerate (in nsIXPConnectWrappedNative wrapper, in JSContextPtr cx, in JSObjectPtr obj, in PRUint32 enum_op, in JSValPtr statep, out JSID idp); */
NS_IMETHODIMP
nsXPCComponents_Results::NewEnumerate(nsIXPConnectWrappedNative *wrapper,
                                      JSContext * cx, JSObject * obj,
                                      PRUint32 enum_op, jsval * statep,
                                      jsid * idp, bool *_retval)
{
    void** iter;

    switch (enum_op) {
        case JSENUMERATE_INIT:
        case JSENUMERATE_INIT_ALL:
        {
            if (idp)
                *idp = INT_TO_JSID(nsXPCException::GetNSResultCount());

            void** space = (void**) new char[sizeof(void*)];
            *space = nsnull;
            *statep = PRIVATE_TO_JSVAL(space);
            return NS_OK;
        }
        case JSENUMERATE_NEXT:
        {
            const char* name;
            iter = (void**) JSVAL_TO_PRIVATE(*statep);
            if (nsXPCException::IterateNSResults(nsnull, &name, nsnull, iter)) {
                JSString* idstr = JS_NewStringCopyZ(cx, name);
                if (idstr && JS_ValueToId(cx, STRING_TO_JSVAL(idstr), idp))
                    return NS_OK;
            }
            // else... FALL THROUGH
        }

        case JSENUMERATE_DESTROY:
        default:
            iter = (void**) JSVAL_TO_PRIVATE(*statep);
            delete [] (char*) iter;
            *statep = JSVAL_NULL;
            return NS_OK;
    }
}


/* bool newResolve (in nsIXPConnectWrappedNative wrapper, in JSContextPtr cx, in JSObjectPtr obj, in jsval id, in PRUint32 flags, out JSObjectPtr objp); */
NS_IMETHODIMP
nsXPCComponents_Results::NewResolve(nsIXPConnectWrappedNative *wrapper,
                                    JSContext * cx, JSObject * obj,
                                    jsid id, PRUint32 flags,
                                    JSObject * *objp, bool *_retval)
{
    JSAutoByteString name;

    if (JSID_IS_STRING(id) && name.encode(cx, JSID_TO_STRING(id))) {
        const char* rv_name;
        void* iter = nsnull;
        nsresult rv;
        while (nsXPCException::IterateNSResults(&rv, &rv_name, nsnull, &iter)) {
            if (!strcmp(name.ptr(), rv_name)) {
                jsval val;

                *objp = obj;
                if (!JS_NewNumberValue(cx, (double)rv, &val) ||
                    !JS_DefinePropertyById(cx, obj, id, val,
                                           nsnull, nsnull,
                                           JSPROP_ENUMERATE |
                                           JSPROP_READONLY |
                                           JSPROP_PERMANENT)) {
                    return NS_ERROR_UNEXPECTED;
                }
            }
        }
    }
    return NS_OK;
}

/***************************************************************************/
// JavaScript Constructor for nsIJSID objects (Components.ID)

class nsXPCComponents_ID :
  public nsIXPCComponents_ID,
  public nsIXPCScriptable,
  public nsIClassInfo
{
public:
    // all the interface method declarations...
    NS_DECL_ISUPPORTS
    NS_DECL_NSIXPCCOMPONENTS_ID
    NS_DECL_NSIXPCSCRIPTABLE
    NS_DECL_NSICLASSINFO


public:
    nsXPCComponents_ID();
    virtual ~nsXPCComponents_ID();

private:
    static nsresult CallOrConstruct(nsIXPConnectWrappedNative *wrapper,
                                    JSContext * cx, JSObject * obj,
                                    PRUint32 argc, jsval * argv,
                                    jsval * vp, bool *_retval);
};

/***************************************************************************/
/* void getInterfaces (out PRUint32 count, [array, size_is (count), retval]
                       out nsIIDPtr array); */
NS_IMETHODIMP
nsXPCComponents_ID::GetInterfaces(PRUint32 *aCount, nsIID * **aArray)
{
    const PRUint32 count = 2;
    *aCount = count;
    nsIID **array;
    *aArray = array = static_cast<nsIID**>(nsMemory::Alloc(count * sizeof(nsIID*)));
    if (!array)
        return NS_ERROR_OUT_OF_MEMORY;

    PRUint32 index = 0;
    nsIID* clone;
#define PUSH_IID(id)                                                          \
    clone = static_cast<nsIID *>(nsMemory::Clone(&NS_GET_IID( id ),           \
                                                 sizeof(nsIID)));             \
    if (!clone)                                                               \
        goto oom;                                                             \
    array[index++] = clone;

    PUSH_IID(nsIXPCComponents_ID)
    PUSH_IID(nsIXPCScriptable)
#undef PUSH_IID

    return NS_OK;
oom:
    while (index)
        nsMemory::Free(array[--index]);
    nsMemory::Free(array);
    *aArray = nsnull;
    return NS_ERROR_OUT_OF_MEMORY;
}

/* nsISupports getHelperForLanguage (in PRUint32 language); */
NS_IMETHODIMP
nsXPCComponents_ID::GetHelperForLanguage(PRUint32 language,
                                         nsISupports **retval)
{
    *retval = nsnull;
    return NS_OK;
}

/* readonly attribute string contractID; */
NS_IMETHODIMP
nsXPCComponents_ID::GetContractID(char * *aContractID)
{
    *aContractID = nsnull;
    return NS_ERROR_NOT_AVAILABLE;
}

/* readonly attribute string classDescription; */
NS_IMETHODIMP
nsXPCComponents_ID::GetClassDescription(char * *aClassDescription)
{
    static const char classDescription[] = "XPCComponents_ID";
    *aClassDescription = (char*)nsMemory::Clone(classDescription, sizeof(classDescription));
    return *aClassDescription ? NS_OK : NS_ERROR_OUT_OF_MEMORY;
}

/* readonly attribute nsCIDPtr classID; */
NS_IMETHODIMP
nsXPCComponents_ID::GetClassID(nsCID * *aClassID)
{
    *aClassID = nsnull;
    return NS_OK;
}

/* readonly attribute PRUint32 implementationLanguage; */
NS_IMETHODIMP
nsXPCComponents_ID::GetImplementationLanguage(PRUint32 *aImplementationLanguage)
{
    *aImplementationLanguage = nsIProgrammingLanguage::CPLUSPLUS;
    return NS_OK;
}

/* readonly attribute PRUint32 flags; */
NS_IMETHODIMP
nsXPCComponents_ID::GetFlags(PRUint32 *aFlags)
{
    *aFlags = nsIClassInfo::THREADSAFE;
    return NS_OK;
}

/* [notxpcom] readonly attribute nsCID classIDNoAlloc; */
NS_IMETHODIMP
nsXPCComponents_ID::GetClassIDNoAlloc(nsCID *aClassIDNoAlloc)
{
    return NS_ERROR_NOT_AVAILABLE;
}

nsXPCComponents_ID::nsXPCComponents_ID()
{
}

nsXPCComponents_ID::~nsXPCComponents_ID()
{
    // empty
}

NS_INTERFACE_MAP_BEGIN(nsXPCComponents_ID)
  NS_INTERFACE_MAP_ENTRY(nsIXPCComponents_ID)
  NS_INTERFACE_MAP_ENTRY(nsIXPCScriptable)
  NS_INTERFACE_MAP_ENTRY(nsIClassInfo)
  NS_INTERFACE_MAP_ENTRY_AMBIGUOUS(nsISupports, nsIXPCComponents_ID)
NS_INTERFACE_MAP_END_THREADSAFE

NS_IMPL_THREADSAFE_ADDREF(nsXPCComponents_ID)
NS_IMPL_THREADSAFE_RELEASE(nsXPCComponents_ID)

// The nsIXPCScriptable map declaration that will generate stubs for us...
#define XPC_MAP_CLASSNAME           nsXPCComponents_ID
#define XPC_MAP_QUOTED_CLASSNAME   "nsXPCComponents_ID"
#define                             XPC_MAP_WANT_CALL
#define                             XPC_MAP_WANT_CONSTRUCT
#define                             XPC_MAP_WANT_HASINSTANCE
#define XPC_MAP_FLAGS               nsIXPCScriptable::ALLOW_PROP_MODS_DURING_RESOLVE
#include "xpc_map_end.h" /* This will #undef the above */


/* bool call (in nsIXPConnectWrappedNative wrapper, in JSContextPtr cx, in JSObjectPtr obj, in PRUint32 argc, in JSValPtr argv, in JSValPtr vp); */
NS_IMETHODIMP
nsXPCComponents_ID::Call(nsIXPConnectWrappedNative *wrapper, JSContext * cx, JSObject * obj, PRUint32 argc, jsval * argv, jsval * vp, bool *_retval)
{
    return CallOrConstruct(wrapper, cx, obj, argc, argv, vp, _retval);

}

/* bool construct (in nsIXPConnectWrappedNative wrapper, in JSContextPtr cx, in JSObjectPtr obj, in PRUint32 argc, in JSValPtr argv, in JSValPtr vp); */
NS_IMETHODIMP
nsXPCComponents_ID::Construct(nsIXPConnectWrappedNative *wrapper, JSContext * cx, JSObject * obj, PRUint32 argc, jsval * argv, jsval * vp, bool *_retval)
{
    return CallOrConstruct(wrapper, cx, obj, argc, argv, vp, _retval);
}

// static
nsresult
nsXPCComponents_ID::CallOrConstruct(nsIXPConnectWrappedNative *wrapper,
                                    JSContext * cx, JSObject * obj,
                                    PRUint32 argc, jsval * argv,
                                    jsval * vp, bool *_retval)
{
    // make sure we have at least one arg

    if (!argc)
        return ThrowAndFail(NS_ERROR_XPC_NOT_ENOUGH_ARGS, cx, _retval);

    XPCCallContext ccx(JS_CALLER, cx);
    if (!ccx.IsValid())
        return ThrowAndFail(NS_ERROR_XPC_UNEXPECTED, cx, _retval);

    XPCContext* xpcc = ccx.GetXPCContext();

    // Do the security check if necessary

    nsIXPCSecurityManager* sm =
            xpcc->GetAppropriateSecurityManager(nsIXPCSecurityManager::HOOK_CREATE_INSTANCE);
    if (sm && NS_FAILED(sm->CanCreateInstance(cx, nsJSID::GetCID()))) {
        // the security manager vetoed. It should have set an exception.
        *_retval = false;
        return NS_OK;
    }

    // convert the first argument into a string and see if it looks like an id

    JSString* jsstr;
    JSAutoByteString bytes;
    nsID id;

    if (!(jsstr = JS_ValueToString(cx, argv[0])) ||
        !bytes.encode(cx, jsstr) ||
        !id.Parse(bytes.ptr())) {
        return ThrowAndFail(NS_ERROR_XPC_BAD_ID_STRING, cx, _retval);
    }

    // make the new object and return it.

    JSObject* newobj = xpc_NewIDObject(cx, obj, id);

    if (vp)
        *vp = OBJECT_TO_JSVAL(newobj);

    return NS_OK;
}

/* bool hasInstance (in nsIXPConnectWrappedNative wrapper, in JSContextPtr cx, in JSObjectPtr obj, in jsval val, out bool bp); */
NS_IMETHODIMP
nsXPCComponents_ID::HasInstance(nsIXPConnectWrappedNative *wrapper,
                                JSContext * cx, JSObject * obj,
                                const jsval &val, bool *bp, bool *_retval)
{
    if (bp)
        *bp = JSValIsInterfaceOfType(cx, val, NS_GET_IID(nsIJSID));
    return NS_OK;
}

/***************************************************************************/
// JavaScript Constructor for nsIXPCException objects (Components.Exception)

class nsXPCComponents_Exception :
  public nsIXPCComponents_Exception,
  public nsIXPCScriptable,
  public nsIClassInfo
{
public:
    // all the interface method declarations...
    NS_DECL_ISUPPORTS
    NS_DECL_NSIXPCCOMPONENTS_EXCEPTION
    NS_DECL_NSIXPCSCRIPTABLE
    NS_DECL_NSICLASSINFO


public:
    nsXPCComponents_Exception();
    virtual ~nsXPCComponents_Exception();

private:
    static nsresult CallOrConstruct(nsIXPConnectWrappedNative *wrapper,
                                    JSContext * cx, JSObject * obj,
                                    PRUint32 argc, jsval * argv,
                                    jsval * vp, bool *_retval);
};

/***************************************************************************/
/* void getInterfaces (out PRUint32 count, [array, size_is (count), retval]
                       out nsIIDPtr array); */
NS_IMETHODIMP
nsXPCComponents_Exception::GetInterfaces(PRUint32 *aCount, nsIID * **aArray)
{
    const PRUint32 count = 2;
    *aCount = count;
    nsIID **array;
    *aArray = array = static_cast<nsIID**>(nsMemory::Alloc(count * sizeof(nsIID*)));
    if (!array)
        return NS_ERROR_OUT_OF_MEMORY;

    PRUint32 index = 0;
    nsIID* clone;
#define PUSH_IID(id)                                                          \
    clone = static_cast<nsIID *>(nsMemory::Clone(&NS_GET_IID( id ),           \
                                                 sizeof(nsIID)));             \
    if (!clone)                                                               \
        goto oom;                                                             \
    array[index++] = clone;

    PUSH_IID(nsIXPCComponents_Exception)
    PUSH_IID(nsIXPCScriptable)
#undef PUSH_IID

    return NS_OK;
oom:
    while (index)
        nsMemory::Free(array[--index]);
    nsMemory::Free(array);
    *aArray = nsnull;
    return NS_ERROR_OUT_OF_MEMORY;
}

/* nsISupports getHelperForLanguage (in PRUint32 language); */
NS_IMETHODIMP
nsXPCComponents_Exception::GetHelperForLanguage(PRUint32 language,
                                                nsISupports **retval)
{
    *retval = nsnull;
    return NS_OK;
}

/* readonly attribute string contractID; */
NS_IMETHODIMP
nsXPCComponents_Exception::GetContractID(char * *aContractID)
{
    *aContractID = nsnull;
    return NS_ERROR_NOT_AVAILABLE;
}

/* readonly attribute string classDescription; */
NS_IMETHODIMP
nsXPCComponents_Exception::GetClassDescription(char * *aClassDescription)
{
    static const char classDescription[] = "XPCComponents_Exception";
    *aClassDescription = (char*)nsMemory::Clone(classDescription, sizeof(classDescription));
    return *aClassDescription ? NS_OK : NS_ERROR_OUT_OF_MEMORY;
}

/* readonly attribute nsCIDPtr classID; */
NS_IMETHODIMP
nsXPCComponents_Exception::GetClassID(nsCID * *aClassID)
{
    *aClassID = nsnull;
    return NS_OK;
}

/* readonly attribute PRUint32 implementationLanguage; */
NS_IMETHODIMP
nsXPCComponents_Exception::GetImplementationLanguage(PRUint32 *aImplementationLanguage)
{
    *aImplementationLanguage = nsIProgrammingLanguage::CPLUSPLUS;
    return NS_OK;
}

/* readonly attribute PRUint32 flags; */
NS_IMETHODIMP
nsXPCComponents_Exception::GetFlags(PRUint32 *aFlags)
{
    *aFlags = nsIClassInfo::THREADSAFE;
    return NS_OK;
}

/* [notxpcom] readonly attribute nsCID classIDNoAlloc; */
NS_IMETHODIMP
nsXPCComponents_Exception::GetClassIDNoAlloc(nsCID *aClassIDNoAlloc)
{
    return NS_ERROR_NOT_AVAILABLE;
}

nsXPCComponents_Exception::nsXPCComponents_Exception()
{
}

nsXPCComponents_Exception::~nsXPCComponents_Exception()
{
    // empty
}

NS_INTERFACE_MAP_BEGIN(nsXPCComponents_Exception)
  NS_INTERFACE_MAP_ENTRY(nsIXPCComponents_Exception)
  NS_INTERFACE_MAP_ENTRY(nsIXPCScriptable)
  NS_INTERFACE_MAP_ENTRY(nsIClassInfo)
  NS_INTERFACE_MAP_ENTRY_AMBIGUOUS(nsISupports, nsIXPCComponents_Exception)
NS_INTERFACE_MAP_END_THREADSAFE

NS_IMPL_THREADSAFE_ADDREF(nsXPCComponents_Exception)
NS_IMPL_THREADSAFE_RELEASE(nsXPCComponents_Exception)

// The nsIXPCScriptable map declaration that will generate stubs for us...
#define XPC_MAP_CLASSNAME           nsXPCComponents_Exception
#define XPC_MAP_QUOTED_CLASSNAME   "nsXPCComponents_Exception"
#define                             XPC_MAP_WANT_CALL
#define                             XPC_MAP_WANT_CONSTRUCT
#define                             XPC_MAP_WANT_HASINSTANCE
#define XPC_MAP_FLAGS               nsIXPCScriptable::ALLOW_PROP_MODS_DURING_RESOLVE
#include "xpc_map_end.h" /* This will #undef the above */


/* bool call (in nsIXPConnectWrappedNative wrapper, in JSContextPtr cx, in JSObjectPtr obj, in PRUint32 argc, in JSValPtr argv, in JSValPtr vp); */
NS_IMETHODIMP
nsXPCComponents_Exception::Call(nsIXPConnectWrappedNative *wrapper, JSContext * cx, JSObject * obj, PRUint32 argc, jsval * argv, jsval * vp, bool *_retval)
{
    return CallOrConstruct(wrapper, cx, obj, argc, argv, vp, _retval);

}

/* bool construct (in nsIXPConnectWrappedNative wrapper, in JSContextPtr cx, in JSObjectPtr obj, in PRUint32 argc, in JSValPtr argv, in JSValPtr vp); */
NS_IMETHODIMP
nsXPCComponents_Exception::Construct(nsIXPConnectWrappedNative *wrapper, JSContext * cx, JSObject * obj, PRUint32 argc, jsval * argv, jsval * vp, bool *_retval)
{
    return CallOrConstruct(wrapper, cx, obj, argc, argv, vp, _retval);
}

struct NS_STACK_CLASS ExceptionArgParser
{
    ExceptionArgParser(JSContext *context,
                       nsXPConnect *xpconnect)
        : eMsg("exception")
        , eResult(NS_ERROR_FAILURE)
        , cx(context)
        , xpc(xpconnect)
    {}

    // Public exception parameter values. During construction, these are
    // initialized to the appropriate defaults.
    const char*             eMsg;
    nsresult                eResult;
    nsCOMPtr<nsIStackFrame> eStack;
    nsCOMPtr<nsISupports>   eData;

    // Parse the constructor arguments into the above |eFoo| parameter values.
    bool parse(uint32_t argc, JS::Value *argv) {
        /*
         * The Components.Exception takes a series of arguments, all of them
         * optional:
         *
         * Argument 0: Exception message (defaults to 'exception').
         * Argument 1: Result code (defaults to NS_ERROR_FAILURE) _or_ options
         *             object (see below).
         * Argument 2: Stack (defaults to the current stack, which we trigger
         *                    by leaving this NULL in the parser).
         * Argument 3: Optional user data (defaults to NULL).
         *
         * To dig our way out of this clunky API, we now support passing an
         * options object as the second parameter (as opposed to a result code).
         * If this is the case, all subsequent arguments are ignored, and the
         * following properties are parsed out of the object (using the
         * associated default if the property does not exist):
         *
         *   result:    Result code (see argument 1).
         *   stack:     Call stack (see argument 2).
         *   data:      User data (see argument 3).
         */
        if (argc > 0 && !parseMessage(argv[0]))
            return false;
        if (argc > 1) {
            if (argv[1].isObject())
                return parseOptionsObject(argv[1].toObject());
            if (!parseResult(argv[1]))
                return false;
        }
        if (argc > 2 && !parseStack(argv[2]))
            return false;
        if (argc > 3 && !parseData(argv[3]))
            return false;
        return true;
    }

  protected:

    /*
     * Parsing helpers.
     */

    bool parseMessage(JS::Value &v) {
        JSString *str = JS_ValueToString(cx, v);
        if (!str)
           return false;
        eMsg = messageBytes.encode(cx, str);
        return !!eMsg;
    }

    bool parseResult(JS::Value &v) {
        return JS_ValueToECMAInt32(cx, v, (int32_t*) &eResult);
    }

    bool parseStack(JS::Value &v) {
        if (!v.isObject()) {
            // eStack has already been initialized to null, which is what we want
            // for any non-object values (including null).
            return true;
        }

        return NS_SUCCEEDED(xpc->WrapJS(cx, JSVAL_TO_OBJECT(v),
                                        NS_GET_IID(nsIStackFrame),
                                        getter_AddRefs(eStack)));
    }

    bool parseData(JS::Value &v) {
        if (!v.isObject()) {
            // eData has already been initialized to null, which is what we want
            // for any non-object values (including null).
            return true;
        }

        return NS_SUCCEEDED(xpc->WrapJS(cx, &v.toObject(),
                                        NS_GET_IID(nsISupports),
                                        getter_AddRefs(eData)));
    }

    bool parseOptionsObject(JSObject &obj) {
        JS::Value v;

        if (!getOption(obj, "result", &v) ||
            (!v.isUndefined() && !parseResult(v)))
            return false;

        if (!getOption(obj, "stack", &v) ||
            (!v.isUndefined() && !parseStack(v)))
            return false;

        if (!getOption(obj, "data", &v) ||
            (!v.isUndefined() && !parseData(v)))
            return false;

        return true;
    }

    bool getOption(JSObject &obj, const char *name, JS::Value *rv) {
        // Look for the property.
        JSBool found;
        if (!JS_HasProperty(cx, &obj, name, &found))
            return false;

        // If it wasn't found, indicate with undefined.
        if (!found) {
            *rv = JSVAL_VOID;
            return true;
        }

        // Get the property.
        return JS_GetProperty(cx, &obj, name, rv);
    }

    /*
     * Internal data members.
     */

    // If there's a non-default exception string, hold onto the allocated bytes.
    JSAutoByteString messageBytes;

    // Various bits and pieces that are helpful to have around.
    JSContext *cx;
    nsXPConnect *xpc;
};

// static
nsresult
nsXPCComponents_Exception::CallOrConstruct(nsIXPConnectWrappedNative *wrapper,
                                           JSContext * cx, JSObject * obj,
                                           PRUint32 argc, jsval * argv,
                                           jsval * vp, bool *_retval)
{
    XPCCallContext ccx(JS_CALLER, cx);
    if (!ccx.IsValid())
        return ThrowAndFail(NS_ERROR_XPC_UNEXPECTED, cx, _retval);

    nsXPConnect* xpc = ccx.GetXPConnect();
    XPCContext* xpcc = ccx.GetXPCContext();

    // Do the security check if necessary

    nsIXPCSecurityManager* sm =
            xpcc->GetAppropriateSecurityManager(nsIXPCSecurityManager::HOOK_CREATE_INSTANCE);
    if (sm && NS_FAILED(sm->CanCreateInstance(cx, nsXPCException::GetCID()))) {
        // the security manager vetoed. It should have set an exception.
        *_retval = false;
        return NS_OK;
    }

    // Parse the arguments to the Exception constructor.
    ExceptionArgParser args(cx, xpc);
    if (!args.parse(argc, argv))
        return ThrowAndFail(NS_ERROR_XPC_BAD_CONVERT_JS, cx, _retval);

    nsCOMPtr<nsIException> e;
    nsXPCException::NewException(args.eMsg, args.eResult, args.eStack,
                                 args.eData, getter_AddRefs(e));
    if (!e)
        return ThrowAndFail(NS_ERROR_XPC_UNEXPECTED, cx, _retval);

    nsCOMPtr<nsIXPConnectJSObjectHolder> holder;
    JSObject* newObj = nsnull;

    if (NS_FAILED(xpc->WrapNative(cx, obj, e, NS_GET_IID(nsIXPCException),
                                  getter_AddRefs(holder))) || !holder ||
        NS_FAILED(holder->GetJSObject(&newObj)) || !newObj) {
        return ThrowAndFail(NS_ERROR_XPC_CANT_CREATE_WN, cx, _retval);
    }

    if (vp)
        *vp = OBJECT_TO_JSVAL(newObj);

    return NS_OK;
}

/* bool hasInstance (in nsIXPConnectWrappedNative wrapper, in JSContextPtr cx, in JSObjectPtr obj, in jsval val, out bool bp); */
NS_IMETHODIMP
nsXPCComponents_Exception::HasInstance(nsIXPConnectWrappedNative *wrapper,
                                       JSContext * cx, JSObject * obj,
                                       const jsval &val, bool *bp,
                                       bool *_retval)
{
    if (bp)
        *bp = JSValIsInterfaceOfType(cx, val, NS_GET_IID(nsIException));
    return NS_OK;
}

/***************************************************************************/
// This class is for the thing returned by "new Component.Constructor".

// XXXjband we use this CID for security check, but security system can't see
// it since it has no registed factory. Security really kicks in when we try
// to build a wrapper around an instance.

// {B4A95150-E25A-11d3-8F61-0010A4E73D9A}
#define NS_XPCCONSTRUCTOR_CID                                                 \
{ 0xb4a95150, 0xe25a, 0x11d3,                                                 \
    { 0x8f, 0x61, 0x0, 0x10, 0xa4, 0xe7, 0x3d, 0x9a } }

class nsXPCConstructor :
  public nsIXPCConstructor,
  public nsIXPCScriptable,
  public nsIClassInfo
{
public:
    NS_DEFINE_STATIC_CID_ACCESSOR(NS_XPCCONSTRUCTOR_CID)
public:
    // all the interface method declarations...
    NS_DECL_ISUPPORTS
    NS_DECL_NSIXPCCONSTRUCTOR
    NS_DECL_NSIXPCSCRIPTABLE
    NS_DECL_NSICLASSINFO

public:
    nsXPCConstructor(); // not implemented
    nsXPCConstructor(nsIJSCID* aClassID,
                     nsIJSIID* aInterfaceID,
                     const char* aInitializer);
    virtual ~nsXPCConstructor();

private:
    nsresult CallOrConstruct(nsIXPConnectWrappedNative *wrapper,
                             JSContext * cx, JSObject * obj,
                             PRUint32 argc, jsval * argv,
                             jsval * vp, bool *_retval);
private:
    nsIJSCID* mClassID;
    nsIJSIID* mInterfaceID;
    char*     mInitializer;
};

/***************************************************************************/
/* void getInterfaces (out PRUint32 count, [array, size_is (count), retval]
                       out nsIIDPtr array); */
NS_IMETHODIMP
nsXPCConstructor::GetInterfaces(PRUint32 *aCount, nsIID * **aArray)
{
    const PRUint32 count = 2;
    *aCount = count;
    nsIID **array;
    *aArray = array = static_cast<nsIID**>(nsMemory::Alloc(count * sizeof(nsIID*)));
    if (!array)
        return NS_ERROR_OUT_OF_MEMORY;

    PRUint32 index = 0;
    nsIID* clone;
#define PUSH_IID(id)                                                          \
    clone = static_cast<nsIID *>(nsMemory::Clone(&NS_GET_IID( id ),           \
                                                 sizeof(nsIID)));             \
    if (!clone)                                                               \
        goto oom;                                                             \
    array[index++] = clone;

    PUSH_IID(nsIXPCConstructor)
    PUSH_IID(nsIXPCScriptable)
#undef PUSH_IID

    return NS_OK;
oom:
    while (index)
        nsMemory::Free(array[--index]);
    nsMemory::Free(array);
    *aArray = nsnull;
    return NS_ERROR_OUT_OF_MEMORY;
}

/* nsISupports getHelperForLanguage (in PRUint32 language); */
NS_IMETHODIMP
nsXPCConstructor::GetHelperForLanguage(PRUint32 language,
                                       nsISupports **retval)
{
    *retval = nsnull;
    return NS_OK;
}

/* readonly attribute string contractID; */
NS_IMETHODIMP
nsXPCConstructor::GetContractID(char * *aContractID)
{
    *aContractID = nsnull;
    return NS_ERROR_NOT_AVAILABLE;
}

/* readonly attribute string classDescription; */
NS_IMETHODIMP
nsXPCConstructor::GetClassDescription(char * *aClassDescription)
{
    static const char classDescription[] = "XPCConstructor";
    *aClassDescription = (char*)nsMemory::Clone(classDescription, sizeof(classDescription));
    return *aClassDescription ? NS_OK : NS_ERROR_OUT_OF_MEMORY;
}

/* readonly attribute nsCIDPtr classID; */
NS_IMETHODIMP
nsXPCConstructor::GetClassID(nsCID * *aClassID)
{
    *aClassID = nsnull;
    return NS_OK;
}

/* readonly attribute PRUint32 implementationLanguage; */
NS_IMETHODIMP
nsXPCConstructor::GetImplementationLanguage(PRUint32 *aImplementationLanguage)
{
    *aImplementationLanguage = nsIProgrammingLanguage::CPLUSPLUS;
    return NS_OK;
}

/* readonly attribute PRUint32 flags; */
NS_IMETHODIMP
nsXPCConstructor::GetFlags(PRUint32 *aFlags)
{
    *aFlags = nsIClassInfo::THREADSAFE;
    return NS_OK;
}

/* [notxpcom] readonly attribute nsCID classIDNoAlloc; */
NS_IMETHODIMP
nsXPCConstructor::GetClassIDNoAlloc(nsCID *aClassIDNoAlloc)
{
    return NS_ERROR_NOT_AVAILABLE;
}

nsXPCConstructor::nsXPCConstructor(nsIJSCID* aClassID,
                                   nsIJSIID* aInterfaceID,
                                   const char* aInitializer)
{
    NS_IF_ADDREF(mClassID = aClassID);
    NS_IF_ADDREF(mInterfaceID = aInterfaceID);
    mInitializer = aInitializer ?
        (char*) nsMemory::Clone(aInitializer, strlen(aInitializer)+1) :
        nsnull;
}

nsXPCConstructor::~nsXPCConstructor()
{
    NS_IF_RELEASE(mClassID);
    NS_IF_RELEASE(mInterfaceID);
    if (mInitializer)
        nsMemory::Free(mInitializer);
}

/* readonly attribute nsIJSCID classID; */
NS_IMETHODIMP
nsXPCConstructor::GetClassID(nsIJSCID * *aClassID)
{
    NS_IF_ADDREF(*aClassID = mClassID);
    return NS_OK;
}

/* readonly attribute nsIJSIID interfaceID; */
NS_IMETHODIMP
nsXPCConstructor::GetInterfaceID(nsIJSIID * *aInterfaceID)
{
    NS_IF_ADDREF(*aInterfaceID = mInterfaceID);
    return NS_OK;
}

/* readonly attribute string initializer; */
NS_IMETHODIMP
nsXPCConstructor::GetInitializer(char * *aInitializer)
{
    XPC_STRING_GETTER_BODY(aInitializer, mInitializer);
}

NS_INTERFACE_MAP_BEGIN(nsXPCConstructor)
  NS_INTERFACE_MAP_ENTRY(nsIXPCConstructor)
  NS_INTERFACE_MAP_ENTRY(nsIXPCScriptable)
  NS_INTERFACE_MAP_ENTRY(nsIClassInfo)
  NS_INTERFACE_MAP_ENTRY_AMBIGUOUS(nsISupports, nsIXPCConstructor)
NS_INTERFACE_MAP_END_THREADSAFE

NS_IMPL_THREADSAFE_ADDREF(nsXPCConstructor)
NS_IMPL_THREADSAFE_RELEASE(nsXPCConstructor)

// The nsIXPCScriptable map declaration that will generate stubs for us...
#define XPC_MAP_CLASSNAME           nsXPCConstructor
#define XPC_MAP_QUOTED_CLASSNAME   "nsXPCConstructor"
#define                             XPC_MAP_WANT_CALL
#define                             XPC_MAP_WANT_CONSTRUCT
#define XPC_MAP_FLAGS               0
#include "xpc_map_end.h" /* This will #undef the above */


/* bool call (in nsIXPConnectWrappedNative wrapper, in JSContextPtr cx, in JSObjectPtr obj, in PRUint32 argc, in JSValPtr argv, in JSValPtr vp); */
NS_IMETHODIMP
nsXPCConstructor::Call(nsIXPConnectWrappedNative *wrapper, JSContext * cx, JSObject * obj, PRUint32 argc, jsval * argv, jsval * vp, bool *_retval)
{
    return CallOrConstruct(wrapper, cx, obj, argc, argv, vp, _retval);

}

/* bool construct (in nsIXPConnectWrappedNative wrapper, in JSContextPtr cx, in JSObjectPtr obj, in PRUint32 argc, in JSValPtr argv, in JSValPtr vp); */
NS_IMETHODIMP
nsXPCConstructor::Construct(nsIXPConnectWrappedNative *wrapper, JSContext * cx, JSObject * obj, PRUint32 argc, jsval * argv, jsval * vp, bool *_retval)
{
    return CallOrConstruct(wrapper, cx, obj, argc, argv, vp, _retval);
}

// static
nsresult
nsXPCConstructor::CallOrConstruct(nsIXPConnectWrappedNative *wrapper,
                                  JSContext * cx, JSObject * obj,
                                  PRUint32 argc, jsval * argv,
                                  jsval * vp, bool *_retval)
{
    XPCCallContext ccx(JS_CALLER, cx);
    if (!ccx.IsValid())
        return ThrowAndFail(NS_ERROR_XPC_UNEXPECTED, cx, _retval);

    nsXPConnect* xpc = ccx.GetXPConnect();

    // security check not required because we are going to call through the
    // code which is reflected into JS which will do that for us later.

    nsCOMPtr<nsIXPConnectJSObjectHolder> cidHolder;
    nsCOMPtr<nsIXPConnectJSObjectHolder> iidHolder;
    JSObject* cidObj;
    JSObject* iidObj;

    if (NS_FAILED(xpc->WrapNative(cx, obj, mClassID, NS_GET_IID(nsIJSCID),
                                  getter_AddRefs(cidHolder))) || !cidHolder ||
        NS_FAILED(cidHolder->GetJSObject(&cidObj)) || !cidObj ||
        NS_FAILED(xpc->WrapNative(cx, obj, mInterfaceID, NS_GET_IID(nsIJSIID),
                                  getter_AddRefs(iidHolder))) || !iidHolder ||
        NS_FAILED(iidHolder->GetJSObject(&iidObj)) || !iidObj) {
        return ThrowAndFail(NS_ERROR_XPC_CANT_CREATE_WN, cx, _retval);
    }

    jsval ctorArgs[1] = {OBJECT_TO_JSVAL(iidObj)};
    jsval val;

    if (!JS_CallFunctionName(cx, cidObj, "createInstance", 1, ctorArgs, &val) ||
        JSVAL_IS_PRIMITIVE(val)) {
        // createInstance will have thrown an exception
        *_retval = false;
        return NS_OK;
    }

    // root the result
    if (vp)
        *vp = val;

    // call initializer method if supplied
    if (mInitializer) {
        JSObject* newObj = JSVAL_TO_OBJECT(val);
        jsval fun;
        jsval ignored;

        // first check existence of function property for better error reporting
        if (!JS_GetProperty(cx, newObj, mInitializer, &fun) ||
            JSVAL_IS_PRIMITIVE(fun)) {
            return ThrowAndFail(NS_ERROR_XPC_BAD_INITIALIZER_NAME, cx, _retval);
        }

        if (!JS_CallFunctionValue(cx, newObj, fun, argc, argv, &ignored)) {
            // function should have thrown an exception
            *_retval = false;
            return NS_OK;
        }
    }

    return NS_OK;
}

/*******************************************************/
// JavaScript Constructor for nsIXPCConstructor objects (Components.Constructor)

class nsXPCComponents_Constructor :
  public nsIXPCComponents_Constructor,
  public nsIXPCScriptable,
  public nsIClassInfo
{
public:
    // all the interface method declarations...
    NS_DECL_ISUPPORTS
    NS_DECL_NSIXPCCOMPONENTS_CONSTRUCTOR
    NS_DECL_NSIXPCSCRIPTABLE
    NS_DECL_NSICLASSINFO

public:
    nsXPCComponents_Constructor();
    virtual ~nsXPCComponents_Constructor();

private:
    static nsresult CallOrConstruct(nsIXPConnectWrappedNative *wrapper,
                                    JSContext * cx, JSObject * obj,
                                    PRUint32 argc, jsval * argv,
                                    jsval * vp, bool *_retval);
};

/***************************************************************************/
/* void getInterfaces (out PRUint32 count, [array, size_is (count), retval]
                       out nsIIDPtr array); */
NS_IMETHODIMP
nsXPCComponents_Constructor::GetInterfaces(PRUint32 *aCount, nsIID * **aArray)
{
    const PRUint32 count = 2;
    *aCount = count;
    nsIID **array;
    *aArray = array = static_cast<nsIID**>(nsMemory::Alloc(count * sizeof(nsIID*)));
    if (!array)
        return NS_ERROR_OUT_OF_MEMORY;

    PRUint32 index = 0;
    nsIID* clone;
#define PUSH_IID(id)                                                          \
    clone = static_cast<nsIID *>(nsMemory::Clone(&NS_GET_IID( id ),           \
                                                 sizeof(nsIID)));             \
    if (!clone)                                                               \
        goto oom;                                                             \
    array[index++] = clone;

    PUSH_IID(nsIXPCComponents_Constructor)
    PUSH_IID(nsIXPCScriptable)
#undef PUSH_IID

    return NS_OK;
oom:
    while (index)
        nsMemory::Free(array[--index]);
    nsMemory::Free(array);
    *aArray = nsnull;
    return NS_ERROR_OUT_OF_MEMORY;
}

/* nsISupports getHelperForLanguage (in PRUint32 language); */
NS_IMETHODIMP
nsXPCComponents_Constructor::GetHelperForLanguage(PRUint32 language,
                                                  nsISupports **retval)
{
    *retval = nsnull;
    return NS_OK;
}

/* readonly attribute string contractID; */
NS_IMETHODIMP
nsXPCComponents_Constructor::GetContractID(char * *aContractID)
{
    *aContractID = nsnull;
    return NS_ERROR_NOT_AVAILABLE;
}

/* readonly attribute string classDescription; */
NS_IMETHODIMP
nsXPCComponents_Constructor::GetClassDescription(char * *aClassDescription)
{
    static const char classDescription[] = "XPCComponents_Constructor";
    *aClassDescription = (char*)nsMemory::Clone(classDescription, sizeof(classDescription));
    return *aClassDescription ? NS_OK : NS_ERROR_OUT_OF_MEMORY;
}

/* readonly attribute nsCIDPtr classID; */
NS_IMETHODIMP
nsXPCComponents_Constructor::GetClassID(nsCID * *aClassID)
{
    *aClassID = nsnull;
    return NS_OK;
}

/* readonly attribute PRUint32 implementationLanguage; */
NS_IMETHODIMP
nsXPCComponents_Constructor::GetImplementationLanguage(PRUint32 *aImplementationLanguage)
{
    *aImplementationLanguage = nsIProgrammingLanguage::CPLUSPLUS;
    return NS_OK;
}

/* readonly attribute PRUint32 flags; */
NS_IMETHODIMP
nsXPCComponents_Constructor::GetFlags(PRUint32 *aFlags)
{
    *aFlags = nsIClassInfo::THREADSAFE;
    return NS_OK;
}

/* [notxpcom] readonly attribute nsCID classIDNoAlloc; */
NS_IMETHODIMP
nsXPCComponents_Constructor::GetClassIDNoAlloc(nsCID *aClassIDNoAlloc)
{
    return NS_ERROR_NOT_AVAILABLE;
}

nsXPCComponents_Constructor::nsXPCComponents_Constructor()
{
}

nsXPCComponents_Constructor::~nsXPCComponents_Constructor()
{
    // empty
}

NS_INTERFACE_MAP_BEGIN(nsXPCComponents_Constructor)
  NS_INTERFACE_MAP_ENTRY(nsIXPCComponents_Constructor)
  NS_INTERFACE_MAP_ENTRY(nsIXPCScriptable)
  NS_INTERFACE_MAP_ENTRY(nsIClassInfo)
  NS_INTERFACE_MAP_ENTRY_AMBIGUOUS(nsISupports, nsIXPCComponents_Constructor)
NS_INTERFACE_MAP_END_THREADSAFE

NS_IMPL_THREADSAFE_ADDREF(nsXPCComponents_Constructor)
NS_IMPL_THREADSAFE_RELEASE(nsXPCComponents_Constructor)

// The nsIXPCScriptable map declaration that will generate stubs for us...
#define XPC_MAP_CLASSNAME           nsXPCComponents_Constructor
#define XPC_MAP_QUOTED_CLASSNAME   "nsXPCComponents_Constructor"
#define                             XPC_MAP_WANT_CALL
#define                             XPC_MAP_WANT_CONSTRUCT
#define                             XPC_MAP_WANT_HASINSTANCE
#define XPC_MAP_FLAGS               nsIXPCScriptable::ALLOW_PROP_MODS_DURING_RESOLVE
#include "xpc_map_end.h" /* This will #undef the above */


/* bool call (in nsIXPConnectWrappedNative wrapper, in JSContextPtr cx, in JSObjectPtr obj, in PRUint32 argc, in JSValPtr argv, in JSValPtr vp); */
NS_IMETHODIMP
nsXPCComponents_Constructor::Call(nsIXPConnectWrappedNative *wrapper, JSContext * cx, JSObject * obj, PRUint32 argc, jsval * argv, jsval * vp, bool *_retval)
{
    return CallOrConstruct(wrapper, cx, obj, argc, argv, vp, _retval);
}

/* bool construct (in nsIXPConnectWrappedNative wrapper, in JSContextPtr cx, in JSObjectPtr obj, in PRUint32 argc, in JSValPtr argv, in JSValPtr vp); */
NS_IMETHODIMP
nsXPCComponents_Constructor::Construct(nsIXPConnectWrappedNative *wrapper, JSContext * cx, JSObject * obj, PRUint32 argc, jsval * argv, jsval * vp, bool *_retval)
{
    return CallOrConstruct(wrapper, cx, obj, argc, argv, vp, _retval);
}

// static
nsresult
nsXPCComponents_Constructor::CallOrConstruct(nsIXPConnectWrappedNative *wrapper,
                                             JSContext * cx, JSObject * obj,
                                             PRUint32 argc, jsval * argv,
                                             jsval * vp, bool *_retval)
{
    // make sure we have at least one arg

    if (!argc)
        return ThrowAndFail(NS_ERROR_XPC_NOT_ENOUGH_ARGS, cx, _retval);

    // get the various other object pointers we need

    XPCCallContext ccx(JS_CALLER, cx);
    if (!ccx.IsValid())
        return ThrowAndFail(NS_ERROR_XPC_UNEXPECTED, cx, _retval);

    nsXPConnect* xpc = ccx.GetXPConnect();
    XPCContext* xpcc = ccx.GetXPCContext();
    XPCWrappedNativeScope* scope =
        XPCWrappedNativeScope::FindInJSObjectScope(ccx, obj);
    nsXPCComponents* comp;

    if (!xpc || !xpcc || !scope || !(comp = scope->GetComponents()))
        return ThrowAndFail(NS_ERROR_XPC_UNEXPECTED, cx, _retval);

    // Do the security check if necessary

    nsIXPCSecurityManager* sm =
            xpcc->GetAppropriateSecurityManager(nsIXPCSecurityManager::HOOK_CREATE_INSTANCE);
    if (sm && NS_FAILED(sm->CanCreateInstance(cx, nsXPCConstructor::GetCID()))) {
        // the security manager vetoed. It should have set an exception.
        *_retval = false;
        return NS_OK;
    }

    // initialization params for the Constructor object we will create
    nsCOMPtr<nsIJSCID> cClassID;
    nsCOMPtr<nsIJSIID> cInterfaceID;
    const char*        cInitializer = nsnull;
    JSAutoByteString  cInitializerBytes;

    if (argc >= 3) {
        // argv[2] is an initializer function or property name
        JSString* str = JS_ValueToString(cx, argv[2]);
        if (!str || !(cInitializer = cInitializerBytes.encode(cx, str)))
            return ThrowAndFail(NS_ERROR_XPC_BAD_CONVERT_JS, cx, _retval);
    }

    if (argc >= 2) {
        // argv[1] is an iid name string
        // XXXjband support passing "Components.interfaces.foo"?

        nsCOMPtr<nsIXPCComponents_Interfaces> ifaces;
        nsCOMPtr<nsIXPConnectJSObjectHolder> holder;
        JSObject* ifacesObj = nsnull;

        // we do the lookup by asking the Components.interfaces object
        // for the property with this name - i.e. we let its caching of these
        // nsIJSIID objects work for us.

        if (NS_FAILED(comp->GetInterfaces(getter_AddRefs(ifaces))) ||
            NS_FAILED(xpc->WrapNative(cx, obj, ifaces,
                                      NS_GET_IID(nsIXPCComponents_Interfaces),
                                      getter_AddRefs(holder))) || !holder ||
            NS_FAILED(holder->GetJSObject(&ifacesObj)) || !ifacesObj) {
            return ThrowAndFail(NS_ERROR_XPC_UNEXPECTED, cx, _retval);
        }

        JSString* str = JS_ValueToString(cx, argv[1]);
        jsid id;
        if (!str || !JS_ValueToId(cx, STRING_TO_JSVAL(str), &id))
            return ThrowAndFail(NS_ERROR_XPC_BAD_CONVERT_JS, cx, _retval);

        jsval val;
        if (!JS_GetPropertyById(cx, ifacesObj, id, &val) || JSVAL_IS_PRIMITIVE(val))
            return ThrowAndFail(NS_ERROR_XPC_BAD_IID, cx, _retval);

        nsCOMPtr<nsIXPConnectWrappedNative> wn;
        if (NS_FAILED(xpc->GetWrappedNativeOfJSObject(cx, JSVAL_TO_OBJECT(val),
                                                      getter_AddRefs(wn))) || !wn ||
            !(cInterfaceID = do_QueryWrappedNative(wn))) {
            return ThrowAndFail(NS_ERROR_XPC_UNEXPECTED, cx, _retval);
        }
    } else {
        nsCOMPtr<nsIInterfaceInfo> info;
        xpc->GetInfoForIID(&NS_GET_IID(nsISupports), getter_AddRefs(info));

        if (info) {
            cInterfaceID =
                dont_AddRef(static_cast<nsIJSIID*>(nsJSIID::NewID(info)));
        }
        if (!cInterfaceID)
            return ThrowAndFail(NS_ERROR_XPC_UNEXPECTED, cx, _retval);
    }

    // a new scope to avoid warnings about shadowed names
    {
        // argv[0] is a contractid name string
        // XXXjband support passing "Components.classes.foo"?

        // we do the lookup by asking the Components.classes object
        // for the property with this name - i.e. we let its caching of these
        // nsIJSCID objects work for us.

        nsCOMPtr<nsIXPCComponents_Classes> classes;
        nsCOMPtr<nsIXPConnectJSObjectHolder> holder;
        JSObject* classesObj = nsnull;

        if (NS_FAILED(comp->GetClasses(getter_AddRefs(classes))) ||
            NS_FAILED(xpc->WrapNative(cx, obj, classes,
                                      NS_GET_IID(nsIXPCComponents_Classes),
                                      getter_AddRefs(holder))) || !holder ||
            NS_FAILED(holder->GetJSObject(&classesObj)) || !classesObj) {
            return ThrowAndFail(NS_ERROR_XPC_UNEXPECTED, cx, _retval);
        }

        JSString* str = JS_ValueToString(cx, argv[0]);
        jsid id;
        if (!str || !JS_ValueToId(cx, STRING_TO_JSVAL(str), &id))
            return ThrowAndFail(NS_ERROR_XPC_BAD_CONVERT_JS, cx, _retval);

        jsval val;
        if (!JS_GetPropertyById(cx, classesObj, id, &val) || JSVAL_IS_PRIMITIVE(val))
            return ThrowAndFail(NS_ERROR_XPC_BAD_CID, cx, _retval);

        nsCOMPtr<nsIXPConnectWrappedNative> wn;
        if (NS_FAILED(xpc->GetWrappedNativeOfJSObject(cx, JSVAL_TO_OBJECT(val),
                                                      getter_AddRefs(wn))) || !wn ||
            !(cClassID = do_QueryWrappedNative(wn))) {
            return ThrowAndFail(NS_ERROR_XPC_UNEXPECTED, cx, _retval);
        }
    }

    nsCOMPtr<nsIXPCConstructor> ctor =
        static_cast<nsIXPCConstructor*>
                   (new nsXPCConstructor(cClassID, cInterfaceID, cInitializer));
    if (!ctor)
        return ThrowAndFail(NS_ERROR_XPC_UNEXPECTED, cx, _retval);

    nsCOMPtr<nsIXPConnectJSObjectHolder> holder2;
    JSObject* newObj = nsnull;

    if (NS_FAILED(xpc->WrapNative(cx, obj, ctor, NS_GET_IID(nsIXPCConstructor),
                                  getter_AddRefs(holder2))) || !holder2 ||
        NS_FAILED(holder2->GetJSObject(&newObj)) || !newObj) {
        return ThrowAndFail(NS_ERROR_XPC_CANT_CREATE_WN, cx, _retval);
    }

    if (vp)
        *vp = OBJECT_TO_JSVAL(newObj);

    return NS_OK;
}

/* bool hasInstance (in nsIXPConnectWrappedNative wrapper, in JSContextPtr cx, in JSObjectPtr obj, in jsval val, out bool bp); */
NS_IMETHODIMP
nsXPCComponents_Constructor::HasInstance(nsIXPConnectWrappedNative *wrapper,
                                         JSContext * cx, JSObject * obj,
                                         const jsval &val, bool *bp,
                                         bool *_retval)
{
    if (bp)
        *bp = JSValIsInterfaceOfType(cx, val, NS_GET_IID(nsIXPCConstructor));
    return NS_OK;
}

/***************************************************************************/
// Javascript constructor for the sandbox object
class nsXPCComponents_utils_Sandbox : public nsIXPCComponents_utils_Sandbox,
                                      public nsIXPCScriptable
{
public:
    // Aren't macros nice?
    NS_DECL_ISUPPORTS
    NS_DECL_NSIXPCCOMPONENTS_UTILS_SANDBOX
    NS_DECL_NSIXPCSCRIPTABLE

public:
    nsXPCComponents_utils_Sandbox();
    virtual ~nsXPCComponents_utils_Sandbox();

private:
    static nsresult CallOrConstruct(nsIXPConnectWrappedNative *wrapper,
                                    JSContext * cx, JSObject * obj,
                                    PRUint32 argc, jsval * argv,
                                    jsval * vp, bool *_retval);
};

class nsXPCComponents_Utils :
            public nsIXPCComponents_Utils,
            public nsIXPCScriptable,
            public nsISecurityCheckedComponent
{
public:
    // all the interface method declarations...
    NS_DECL_ISUPPORTS
    NS_DECL_NSIXPCSCRIPTABLE
    NS_DECL_NSISECURITYCHECKEDCOMPONENT
    NS_DECL_NSIXPCCOMPONENTS_UTILS

public:
    nsXPCComponents_Utils() { }
    virtual ~nsXPCComponents_Utils() { }

private:
    nsCOMPtr<nsIXPCComponents_utils_Sandbox> mSandbox;
};

NS_INTERFACE_MAP_BEGIN(nsXPCComponents_Utils)
  NS_INTERFACE_MAP_ENTRY(nsIXPCComponents_Utils)
  NS_INTERFACE_MAP_ENTRY(nsIXPCScriptable)
  NS_INTERFACE_MAP_ENTRY(nsISecurityCheckedComponent)
  NS_INTERFACE_MAP_ENTRY_AMBIGUOUS(nsISupports, nsIXPCComponents_Utils)
NS_INTERFACE_MAP_END_THREADSAFE

NS_IMPL_THREADSAFE_ADDREF(nsXPCComponents_Utils)
NS_IMPL_THREADSAFE_RELEASE(nsXPCComponents_Utils)

// The nsIXPCScriptable map declaration that will generate stubs for us...
#define XPC_MAP_CLASSNAME           nsXPCComponents_Utils
#define XPC_MAP_QUOTED_CLASSNAME   "nsXPCComponents_Utils"
#define XPC_MAP_FLAGS               nsIXPCScriptable::ALLOW_PROP_MODS_DURING_RESOLVE
#include "xpc_map_end.h" /* This will #undef the above */

NS_IMETHODIMP
nsXPCComponents_Utils::GetSandbox(nsIXPCComponents_utils_Sandbox **aSandbox)
{
    NS_ENSURE_ARG_POINTER(aSandbox);
    if (!mSandbox && !(mSandbox = new nsXPCComponents_utils_Sandbox())) {
        *aSandbox = nsnull;
        return NS_ERROR_OUT_OF_MEMORY;
    }
    NS_ADDREF(*aSandbox = mSandbox);
    return NS_OK;
}

/* void lookupMethod (); */
NS_IMETHODIMP
nsXPCComponents_Utils::LookupMethod(const JS::Value& object,
                                    const JS::Value& name,
                                    JSContext *cx,
                                    JS::Value *retval)
{
    JSAutoRequest ar(cx);

    // first param must be a JSObject
    if (JSVAL_IS_PRIMITIVE(object))
        return NS_ERROR_XPC_BAD_CONVERT_JS;

    JSObject* obj = JSVAL_TO_OBJECT(object);
    while (obj && !js::IsWrapper(obj) && !IS_WRAPPER_CLASS(js::GetObjectClass(obj)))
        obj = JS_GetPrototype(obj);

    if (!obj)
        return NS_ERROR_XPC_BAD_CONVERT_JS;

    JSObject* unwrappedObject;
    nsresult rv = nsXPConnect::GetXPConnect()->GetJSObjectOfWrapper(cx, obj, &unwrappedObject);
    if (NS_FAILED(rv))
        return rv;

    unwrappedObject = JS_ObjectToInnerObject(cx, unwrappedObject);
    if (!unwrappedObject)
        return NS_ERROR_XPC_BAD_CONVERT_JS;

    // second param must be a string
    if (!JSVAL_IS_STRING(name))
        return NS_ERROR_XPC_BAD_CONVERT_JS;

    // Make sure the name that we use for looking up the method/property is
    // atomized.
    jsid name_id;
    JS::Value dummy;
    if (!JS_ValueToId(cx, name, &name_id) ||
        !JS_IdToValue(cx, name_id, &dummy))
        return NS_ERROR_XPC_BAD_CONVERT_JS;

    // this will do verification and the method lookup for us
    // Note that if |obj| is an XPCNativeWrapper this will all still work.
    // We'll hand back the same method that we'd hand back for the underlying
    // XPCWrappedNative.  This means no deep wrapping, unfortunately, but we
    // can't keep track of both the underlying function and the
    // XPCNativeWrapper at once in a single parent slot...
    XPCCallContext inner_cc(JS_CALLER, cx, unwrappedObject, nsnull, name_id);

    // was our jsobject really a wrapped native at all?
    XPCWrappedNative* wrapper = inner_cc.GetWrapper();
    if (!wrapper || !wrapper->IsValid())
        return NS_ERROR_XPC_BAD_CONVERT_JS;

    // did we find a method/attribute by that name?
    XPCNativeMember* member = inner_cc.GetMember();
    if (!member || member->IsConstant())
        return NS_ERROR_XPC_BAD_CONVERT_JS;

    // it would a be a big surprise if there is a member without an interface :)
    XPCNativeInterface* iface = inner_cc.GetInterface();
    if (!iface)
        return NS_ERROR_XPC_BAD_CONVERT_JS;

    jsval funval;

    // get (and perhaps lazily create) the member's cloned function
    if (!member->NewFunctionObject(inner_cc, iface, obj, &funval))
        return NS_ERROR_XPC_BAD_CONVERT_JS;

    NS_ASSERTION(JS_ValueToFunction(inner_cc, funval),
                 "Function is not a function");

    // Stick the function in the return value. This roots it.
    *retval = funval;
    return NS_OK;
}

/* void reportError (); */
NS_IMETHODIMP
nsXPCComponents_Utils::ReportError(const JS::Value &error, JSContext *cx)
{
    // This function shall never fail! Silently eat any failure conditions.

    nsCOMPtr<nsIConsoleService> console(do_GetService(NS_CONSOLESERVICE_CONTRACTID));

    nsCOMPtr<nsIScriptError> scripterr(do_CreateInstance(NS_SCRIPTERROR_CONTRACTID));

    if (!scripterr || !console)
        return NS_OK;

    JSAutoRequest ar(cx);

    const PRUint64 innerWindowID = nsJSUtils::GetCurrentlyRunningCodeInnerWindowID(cx);

    JSErrorReport *err = JS_ErrorFromException(cx, error);
    if (err) {
        // It's a proper JS Error
        nsAutoString fileUni;
        CopyUTF8toUTF16(err->filename, fileUni);

        PRUint32 column = err->uctokenptr - err->uclinebuf;

        nsresult rv = scripterr->InitWithWindowID(
                static_cast<const PRUnichar*>(err->ucmessage), fileUni.get(),
                static_cast<const PRUnichar*>(err->uclinebuf), err->lineno,
                column, err->flags, "XPConnect JavaScript", innerWindowID);
        NS_ENSURE_SUCCESS(rv, NS_OK);

        console->LogMessage(scripterr);
        return NS_OK;
    }

    // It's not a JS Error object, so we synthesize as best we're able.
    JSString *msgstr = JS_ValueToString(cx, error);
    if (!msgstr) {
        return NS_OK;
    }

    nsCOMPtr<nsIStackFrame> frame;
    nsXPConnect *xpc = nsXPConnect::GetXPConnect();
    if (xpc)
        xpc->GetCurrentJSStack(getter_AddRefs(frame));

    nsXPIDLCString fileName;
    PRInt32 lineNo = 0;
    if (frame) {
        frame->GetFilename(getter_Copies(fileName));
        frame->GetLineNumber(&lineNo);
    }

    const jschar *msgchars = JS_GetStringCharsZ(cx, msgstr);
    if (!msgchars)
        return NS_OK;

    nsresult rv = scripterr->InitWithWindowID(
            reinterpret_cast<const PRUnichar *>(msgchars),
            NS_ConvertUTF8toUTF16(fileName).get(),
            nsnull, lineNo, 0, 0, "XPConnect JavaScript", innerWindowID);
    NS_ENSURE_SUCCESS(rv, NS_OK);

    console->LogMessage(scripterr);
    return NS_OK;
}

#include "nsIScriptSecurityManager.h"
#include "nsIURI.h"
#include "nsNetUtil.h"
const char kScriptSecurityManagerContractID[] = NS_SCRIPTSECURITYMANAGER_CONTRACTID;

NS_IMPL_THREADSAFE_ISUPPORTS1(PrincipalHolder, nsIScriptObjectPrincipal)

nsIPrincipal *
PrincipalHolder::GetPrincipal()
{
    return mHoldee;
}

static JSBool
SandboxDump(JSContext *cx, unsigned argc, jsval *vp)
{
    JSString *str;
    if (!argc)
        return true;

    str = JS_ValueToString(cx, JS_ARGV(cx, vp)[0]);
    if (!str)
        return false;

    size_t length;
    const jschar *chars = JS_GetStringCharsZAndLength(cx, str, &length);
    if (!chars)
        return false;

    nsDependentString wstr(chars, length);
    char *cstr = ToNewUTF8String(wstr);
    if (!cstr)
        return false;

#if defined(XP_MACOSX)
    // Be nice and convert all \r to \n.
    char *c = cstr, *cEnd = cstr + strlen(cstr);
    while (c < cEnd) {
        if (*c == '\r')
            *c = '\n';
        c++;
    }
#endif

    fputs(cstr, stdout);
    fflush(stdout);
    NS_Free(cstr);
    JS_SET_RVAL(cx, vp, JSVAL_TRUE);
    return true;
}

static JSBool
SandboxDebug(JSContext *cx, unsigned argc, jsval *vp)
{
#ifdef DEBUG
    return SandboxDump(cx, argc, vp);
#else
    return true;
#endif
}

static JSBool
SandboxImport(JSContext *cx, unsigned argc, jsval *vp)
{
    JSObject *thisobj = JS_THIS_OBJECT(cx, vp);
    if (!thisobj)
        return false;

    jsval *argv = JS_ARGV(cx, vp);
    if (argc < 1 || JSVAL_IS_PRIMITIVE(argv[0])) {
        XPCThrower::Throw(NS_ERROR_INVALID_ARG, cx);
        return false;
    }

    JSString *funname;
    if (argc > 1) {
        // Use the second parameter as the function name.
        funname = JS_ValueToString(cx, argv[1]);
        if (!funname)
            return false;
        argv[1] = STRING_TO_JSVAL(funname);
    } else {
        // NB: funobj must only be used to get the JSFunction out.
        JSObject *funobj = JSVAL_TO_OBJECT(argv[0]);
        if (js::IsProxy(funobj)) {
            funobj = XPCWrapper::UnsafeUnwrapSecurityWrapper(funobj);
        }

        JSAutoEnterCompartment ac;
        if (!ac.enter(cx, funobj)) {
            return false;
        }

        JSFunction *fun = JS_ValueToFunction(cx, OBJECT_TO_JSVAL(funobj));
        if (!fun) {
            XPCThrower::Throw(NS_ERROR_INVALID_ARG, cx);
            return false;
        }

        // Use the actual function name as the name.
        funname = JS_GetFunctionId(fun);
        if (!funname) {
            XPCThrower::Throw(NS_ERROR_INVALID_ARG, cx);
            return false;
        }
    }

    jsid id;
    if (!JS_ValueToId(cx, STRING_TO_JSVAL(funname), &id))
        return false;

    JS_SET_RVAL(cx, vp, JSVAL_VOID);
    return JS_SetPropertyById(cx, thisobj, id, &argv[0]);
}

static JSBool
sandbox_enumerate(JSContext *cx, JSObject *obj)
{
    return JS_EnumerateStandardClasses(cx, obj);
}

static JSBool
sandbox_resolve(JSContext *cx, JSObject *obj, jsid id)
{
    JSBool resolved;
    return JS_ResolveStandardClass(cx, obj, id, &resolved);
}

static void
sandbox_finalize(JSFreeOp *fop, JSObject *obj)
{
    nsIScriptObjectPrincipal *sop =
        (nsIScriptObjectPrincipal *)xpc_GetJSPrivate(obj);
    NS_IF_RELEASE(sop);
    DestroyProtoOrIfaceCache(obj);
}

static JSBool
sandbox_convert(JSContext *cx, JSObject *obj, JSType type, jsval *vp)
{
    if (type == JSTYPE_OBJECT) {
        *vp = OBJECT_TO_JSVAL(obj);
        return true;
    }

    return JS_ConvertStub(cx, obj, type, vp);
}

static JSClass SandboxClass = {
    "Sandbox",
    XPCONNECT_GLOBAL_FLAGS,
    JS_PropertyStub,   JS_PropertyStub, JS_PropertyStub, JS_StrictPropertyStub,
    sandbox_enumerate, sandbox_resolve, sandbox_convert,  sandbox_finalize,
    NULL, NULL, NULL, NULL, TraceXPCGlobal
};

static JSFunctionSpec SandboxFunctions[] = {
    {"dump",    SandboxDump,    1,0},
    {"debug",   SandboxDebug,   1,0},
    {"importFunction", SandboxImport, 1,0},
    {nsnull,nsnull,0,0}
};

/***************************************************************************/
nsXPCComponents_utils_Sandbox::nsXPCComponents_utils_Sandbox()
{
}

nsXPCComponents_utils_Sandbox::~nsXPCComponents_utils_Sandbox()
{
}

NS_INTERFACE_MAP_BEGIN(nsXPCComponents_utils_Sandbox)
  NS_INTERFACE_MAP_ENTRY(nsIXPCComponents_utils_Sandbox)
  NS_INTERFACE_MAP_ENTRY(nsIXPCScriptable)
  NS_INTERFACE_MAP_ENTRY_AMBIGUOUS(nsISupports, nsIXPCComponents_utils_Sandbox)
NS_INTERFACE_MAP_END_THREADSAFE

NS_IMPL_THREADSAFE_ADDREF(nsXPCComponents_utils_Sandbox)
NS_IMPL_THREADSAFE_RELEASE(nsXPCComponents_utils_Sandbox)

// We use the nsIXPScriptable macros to generate lots of stuff for us.
#define XPC_MAP_CLASSNAME           nsXPCComponents_utils_Sandbox
#define XPC_MAP_QUOTED_CLASSNAME   "nsXPCComponents_utils_Sandbox"
#define                             XPC_MAP_WANT_CALL
#define                             XPC_MAP_WANT_CONSTRUCT
#define XPC_MAP_FLAGS               0
#include "xpc_map_end.h" /* This #undef's the above. */

static bool
WrapForSandbox(JSContext *cx, bool wantXrays, jsval *vp)
{
    return wantXrays
           ? JS_WrapValue(cx, vp)
           : xpc::WrapperFactory::WaiveXrayAndWrap(cx, vp);
}

// Needed to distinguish multiple compartments with the same origin from each
// other. The only thing we need out of identity objects are unique addresses.
class Identity : public nsISupports
{
    NS_DECL_ISUPPORTS
};

NS_IMPL_ISUPPORTS0(Identity)

xpc::SandboxProxyHandler xpc::sandboxProxyHandler;

template<typename Op>
bool BindPropertyOp(JSContext *cx, JSObject *targetObj, Op& op,
                    PropertyDescriptor *desc, jsid id, unsigned attrFlag)
{
    if (!op) {
        return true;
    }

    JSObject *func;
    if (desc->attrs & attrFlag) {
        // Already an object
        func = JS_FUNC_TO_DATA_PTR(JSObject *, op);
    } else {
        // We have an actual property op.  For getters, we use 0
        // args, for setters we use 1 arg.
        uint32_t args = (attrFlag == JSPROP_GETTER) ? 0 : 1;
        func = GeneratePropertyOp(cx, desc->obj, id, args, op);
        if (!func)
            return false;
    }
    func = JS_BindCallable(cx, func, targetObj);
    if (!func)
        return false;
    op = JS_DATA_TO_FUNC_PTR(Op, func);
    desc->attrs |= attrFlag;
    return true;
}

bool
xpc::SandboxProxyHandler::getPropertyDescriptor(JSContext *cx, JSObject *proxy,
                                                jsid id, bool set,
                                                PropertyDescriptor *desc)
{
    JSObject *obj = wrappedObject(proxy);
    JS_ASSERT(js::GetObjectCompartment(obj) == js::GetObjectCompartment(proxy));
    // XXXbz Not sure about the JSRESOLVE_QUALIFIED here, but we have
    // no way to tell for sure whether to use it.
    if (!JS_GetPropertyDescriptorById(cx, obj, id,
                                      (set ? JSRESOLVE_ASSIGNING : 0) | JSRESOLVE_QUALIFIED,
                                      desc))
        return false;

    if (!desc->obj)
        return true; // No property, nothing to do

    // Now fix up the getter/setter/value as needed to be bound to desc->obj
    // Don't mess with holder_get and holder_set, though, because those rely on
    // the "vp is prefilled with the value in the slot" behavior that property
    // ops can in theory rely on, but our property op forwarder doesn't know how
    // to make that happen.  Since we really only need to rebind the DOM methods
    // here, not rebindings holder_get and holder_set is OK.
    if (desc->getter != xpc::holder_get &&
        !BindPropertyOp(cx, obj, desc->getter, desc, id, JSPROP_GETTER))
        return false;
    if (desc->setter != xpc::holder_set &&
        !BindPropertyOp(cx, obj, desc->setter, desc, id, JSPROP_SETTER))
        return false;
    if (desc->value.isObject()) {
        JSObject* val = &desc->value.toObject();
        if (JS_ObjectIsCallable(cx, val)) {
            val = JS_BindCallable(cx, val, obj);
            if (!val)
                return false;
            desc->value = ObjectValue(*val);
        }
    }

    return true;
}

bool
xpc::SandboxProxyHandler::getOwnPropertyDescriptor(JSContext *cx,
                                                   JSObject *proxy,
                                                   jsid id, bool set,
                                                   PropertyDescriptor *desc)
{
    if (!getPropertyDescriptor(cx, proxy, id, set, desc))
        return false;

    if (desc->obj != wrappedObject(proxy))
        desc->obj = nsnull;

    return true;
}

nsresult
xpc_CreateSandboxObject(JSContext * cx, jsval * vp, nsISupports *prinOrSop, JSObject *proto,
                        bool wantXrays, const nsACString &sandboxName, nsISupports *identityPtr)
{
    // Create the sandbox global object
    nsresult rv;
    nsCOMPtr<nsIXPConnect> xpc(do_GetService(nsIXPConnect::GetCID(), &rv));
    if (NS_FAILED(rv))
        return NS_ERROR_XPC_UNEXPECTED;

    nsCOMPtr<nsIScriptObjectPrincipal> sop(do_QueryInterface(prinOrSop));

    if (!sop) {
        nsCOMPtr<nsIPrincipal> principal(do_QueryInterface(prinOrSop));

        if (!principal) {
            principal = do_CreateInstance("@mozilla.org/nullprincipal;1", &rv);
            NS_ASSERTION(NS_FAILED(rv) || principal,
                         "Bad return from do_CreateInstance");

            if (!principal || NS_FAILED(rv)) {
                if (NS_SUCCEEDED(rv)) {
                    rv = NS_ERROR_FAILURE;
                }

                return rv;
            }
        }

        sop = new PrincipalHolder(principal);
        if (!sop)
            return NS_ERROR_OUT_OF_MEMORY;
    }

    nsIPrincipal *principal = sop->GetPrincipal();

    JSCompartment *compartment;
    JSObject *sandbox;

    nsRefPtr<Identity> identity;
    if (!identityPtr) {
      identity = new Identity();
      identityPtr = identity;
    }

    rv = xpc_CreateGlobalObject(cx, &SandboxClass, principal, identityPtr,
                                wantXrays, &sandbox, &compartment);
    NS_ENSURE_SUCCESS(rv, rv);

    JS::AutoObjectRooter tvr(cx, sandbox);

    {
        JSAutoEnterCompartment ac;
        if (!ac.enter(cx, sandbox))
            return NS_ERROR_XPC_UNEXPECTED;

        if (proto) {
            bool ok = JS_WrapObject(cx, &proto);
            if (!ok)
                return NS_ERROR_XPC_UNEXPECTED;

            if (xpc::WrapperFactory::IsXrayWrapper(proto) && !wantXrays) {
                jsval v = OBJECT_TO_JSVAL(proto);
                if (!xpc::WrapperFactory::WaiveXrayAndWrap(cx, &v))
                    return NS_ERROR_FAILURE;
                proto = JSVAL_TO_OBJECT(v);
            }

            // Now check what sort of thing we've got in |proto|
            JSObject *unwrappedProto = js::UnwrapObject(proto, false);
            js::Class *unwrappedClass = js::GetObjectClass(unwrappedProto);
            if (IS_WRAPPER_CLASS(unwrappedClass) ||
                mozilla::dom::bindings::IsDOMClass(Jsvalify(unwrappedClass))) {
                // Wrap it up in a proxy that will do the right thing in terms
                // of this-binding for methods.
                proto = js::NewProxyObject(cx, &xpc::sandboxProxyHandler,
                                           ObjectValue(*proto), nsnull,
                                           sandbox);
                if (!proto)
                    return NS_ERROR_OUT_OF_MEMORY;
            }

            ok = JS_SetPrototype(cx, sandbox, proto);
            if (!ok)
                return NS_ERROR_XPC_UNEXPECTED;
        }

        // Pass on ownership of sop to |sandbox|.
        JS_SetPrivate(sandbox, sop.forget().get());

        rv = xpc->InitClasses(cx, sandbox);
        if (NS_SUCCEEDED(rv) &&
            !JS_DefineFunctions(cx, sandbox, SandboxFunctions)) {
            rv = NS_ERROR_FAILURE;
        }
        if (NS_FAILED(rv))
            return NS_ERROR_XPC_UNEXPECTED;
    }

    if (vp) {
        *vp = OBJECT_TO_JSVAL(sandbox);
        if (!WrapForSandbox(cx, wantXrays, vp)) {
            return NS_ERROR_UNEXPECTED;
        }
    }

    xpc::CompartmentPrivate *compartmentPrivate =
        static_cast<xpc::CompartmentPrivate*>(JS_GetCompartmentPrivate(compartment));
    compartmentPrivate->location = sandboxName;

    return NS_OK;
}

/* bool call(in nsIXPConnectWrappedNative wrapper,
               in JSContextPtr cx,
               in JSObjectPtr obj,
               in PRUint32 argc,
               in JSValPtr argv,
               in JSValPtr vp);
*/
NS_IMETHODIMP
nsXPCComponents_utils_Sandbox::Call(nsIXPConnectWrappedNative *wrapper,
                                    JSContext * cx,
                                    JSObject * obj,
                                    PRUint32 argc,
                                    jsval * argv,
                                    jsval * vp,
                                    bool *_retval)
{
    return CallOrConstruct(wrapper, cx, obj, argc, argv, vp, _retval);
}

/* bool construct(in nsIXPConnectWrappedNative wrapper,
                    in JSContextPtr cx,
                    in JSObjectPtr obj,
                    in PRUint32 argc,
                    in JSValPtr argv,
                    in JSValPtr vp);
*/
NS_IMETHODIMP
nsXPCComponents_utils_Sandbox::Construct(nsIXPConnectWrappedNative *wrapper,
                                         JSContext * cx,
                                         JSObject * obj,
                                         PRUint32 argc,
                                         jsval * argv,
                                         jsval * vp,
                                         bool *_retval)
{
    return CallOrConstruct(wrapper, cx, obj, argc, argv, vp, _retval);
}

// static
nsresult
nsXPCComponents_utils_Sandbox::CallOrConstruct(nsIXPConnectWrappedNative *wrapper,
                                               JSContext * cx, JSObject * obj,
                                               PRUint32 argc, jsval * argv,
                                               jsval * vp, bool *_retval)
{
    if (argc < 1)
        return ThrowAndFail(NS_ERROR_XPC_NOT_ENOUGH_ARGS, cx, _retval);

    nsresult rv;

    // Make sure to set up principals on the sandbox before initing classes
    nsCOMPtr<nsIScriptObjectPrincipal> sop;
    nsCOMPtr<nsIPrincipal> principal;
    nsISupports *prinOrSop = nsnull;
    nsISupports *identity = nsnull;
    if (JSVAL_IS_STRING(argv[0])) {
        JSString *codebaseStr = JSVAL_TO_STRING(argv[0]);
        size_t codebaseLength;
        const jschar *codebaseChars = JS_GetStringCharsAndLength(cx, codebaseStr,
                                                                 &codebaseLength);
        if (!codebaseChars) {
            return ThrowAndFail(NS_ERROR_FAILURE, cx, _retval);
        }

        nsAutoString codebase(codebaseChars, codebaseLength);
        nsCOMPtr<nsIURI> uri;
        rv = NS_NewURI(getter_AddRefs(uri), codebase);
        if (NS_FAILED(rv)) {
            return ThrowAndFail(rv, cx, _retval);
        }

        nsCOMPtr<nsIScriptSecurityManager> secman =
            do_GetService(kScriptSecurityManagerContractID);
        if (!secman ||
            NS_FAILED(rv = secman->GetCodebasePrincipal(uri,
                                                        getter_AddRefs(principal))) ||
            !principal) {
            if (NS_SUCCEEDED(rv))
                rv = NS_ERROR_FAILURE;
            return ThrowAndFail(rv, cx, _retval);
        }

        prinOrSop = principal;
    } else {
        if (!JSVAL_IS_PRIMITIVE(argv[0])) {
            nsCOMPtr<nsIXPConnect> xpc(do_GetService(nsIXPConnect::GetCID()));
            if (!xpc)
                return NS_ERROR_XPC_UNEXPECTED;
            nsCOMPtr<nsIXPConnectWrappedNative> wrapper;
            xpc->GetWrappedNativeOfJSObject(cx, JSVAL_TO_OBJECT(argv[0]),
                                            getter_AddRefs(wrapper));

            if (wrapper) {
                sop = do_QueryWrappedNative(wrapper);
                if (sop) {
                    prinOrSop = sop;
                } else {
                    principal = do_QueryWrappedNative(wrapper);
                    prinOrSop = principal;
                }
            }
        }

        if (!prinOrSop)
            return ThrowAndFail(NS_ERROR_INVALID_ARG, cx, _retval);
    }

    JSObject *proto = nsnull;
    bool wantXrays = true;
    nsCString sandboxName;

    if (argc > 1) {
        if (!JSVAL_IS_OBJECT(argv[1]))
            return ThrowAndFail(NS_ERROR_INVALID_ARG, cx, _retval);

        JSObject *optionsObject = JSVAL_TO_OBJECT(argv[1]);
        jsval option;

        JSBool found;
        if (!JS_HasProperty(cx, optionsObject, "sandboxPrototype", &found))
            return NS_ERROR_INVALID_ARG;

        if (found) {
            if (!JS_GetProperty(cx, optionsObject, "sandboxPrototype", &option) ||
                !JSVAL_IS_OBJECT(option)) {
                return ThrowAndFail(NS_ERROR_INVALID_ARG, cx, _retval);
            }

            proto = JSVAL_TO_OBJECT(option);
        }

        if (!JS_HasProperty(cx, optionsObject, "wantXrays", &found))
            return NS_ERROR_INVALID_ARG;

        if (found) {
            if (!JS_GetProperty(cx, optionsObject, "wantXrays", &option) ||
                !JSVAL_IS_BOOLEAN(option)) {
                return ThrowAndFail(NS_ERROR_INVALID_ARG, cx, _retval);
            }

            wantXrays = JSVAL_TO_BOOLEAN(option);
        }

        if (!JS_HasProperty(cx, optionsObject, "sandboxName", &found))
            return NS_ERROR_INVALID_ARG;

        if (found) {
            if (!JS_GetProperty(cx, optionsObject, "sandboxName", &option) ||
                !JSVAL_IS_STRING(option)) {
                return ThrowAndFail(NS_ERROR_INVALID_ARG, cx, _retval);
            }

            char *tmp = JS_EncodeString(cx, JSVAL_TO_STRING(option));
            if (!tmp) {
                return ThrowAndFail(NS_ERROR_INVALID_ARG, cx, _retval);
            }

            sandboxName.Adopt(tmp, strlen(tmp));
        }

        // see Bug 677294:
        if (!JS_HasProperty(cx, optionsObject, "sameGroupAs", &found))
            return NS_ERROR_INVALID_ARG;

        if (found) {
            if (!JS_GetProperty(cx, optionsObject, "sameGroupAs", &option) ||
                JSVAL_IS_PRIMITIVE(option))
                return ThrowAndFail(NS_ERROR_INVALID_ARG, cx, _retval);

            JSObject* unwrapped = UnwrapObject(JSVAL_TO_OBJECT(option));
            JSObject* global = GetGlobalForObjectCrossCompartment(unwrapped);
            if (GetObjectJSClass(unwrapped) != &SandboxClass &&
                GetObjectJSClass(global) != &SandboxClass)
                return ThrowAndFail(NS_ERROR_INVALID_ARG, cx, _retval);

            void* privateValue =
                JS_GetCompartmentPrivate(GetObjectCompartment(unwrapped));
            xpc::CompartmentPrivate *compartmentPrivate =
                static_cast<xpc::CompartmentPrivate*>(privateValue);

            if (!compartmentPrivate || !compartmentPrivate->key)
                return ThrowAndFail(NS_ERROR_INVALID_ARG, cx, _retval);

            identity = compartmentPrivate->key->GetPtr();
        }
    }

    // If there is no options object given, or no sandboxName property
    // specified, use the caller's filename as sandboxName.
    if (sandboxName.IsEmpty()) {
        nsXPConnect* xpc = nsXPConnect::GetXPConnect();

        if (!xpc)
            return ThrowAndFail(NS_ERROR_XPC_UNEXPECTED, cx, _retval);

        // Get the xpconnect native call context.
        nsAXPCNativeCallContext *cc = nsnull;
        xpc->GetCurrentNativeCallContext(&cc);

        if (!cc)
            return ThrowAndFail(NS_ERROR_INVALID_ARG, cx, _retval);

        // Get the current source info from xpc.
        nsCOMPtr<nsIStackFrame> frame;
        xpc->GetCurrentJSStack(getter_AddRefs(frame));

        if (frame)
            frame->GetFilename(getter_Copies(sandboxName));
    }

    rv = xpc_CreateSandboxObject(cx, vp, prinOrSop, proto, wantXrays, sandboxName, identity);

    if (NS_FAILED(rv)) {
        return ThrowAndFail(rv, cx, _retval);
    }

    *_retval = true;

    return rv;
}

class ContextHolder : public nsISupports
{
public:
    ContextHolder(JSContext *aOuterCx, JSObject *aSandbox);
    virtual ~ContextHolder();

    JSContext * GetJSContext()
    {
        return mJSContext;
    }

    NS_DECL_ISUPPORTS

private:
    static JSBool ContextHolderOperationCallback(JSContext *cx);

    JSContext* mJSContext;
    JSContext* mOrigCx;
};

NS_IMPL_ISUPPORTS0(ContextHolder)

ContextHolder::ContextHolder(JSContext *aOuterCx, JSObject *aSandbox)
    : mJSContext(JS_NewContext(JS_GetRuntime(aOuterCx), 1024)),
      mOrigCx(aOuterCx)
{
    if (mJSContext) {
        JSAutoRequest ar(mJSContext);
        JS_SetOptions(mJSContext,
                      JS_GetOptions(mJSContext) |
                      JSOPTION_DONT_REPORT_UNCAUGHT |
                      JSOPTION_PRIVATE_IS_NSISUPPORTS);
        JS_SetGlobalObject(mJSContext, aSandbox);
        JS_SetContextPrivate(mJSContext, this);
        JS_SetOperationCallback(mJSContext, ContextHolderOperationCallback);
    }
}

ContextHolder::~ContextHolder()
{
    if (mJSContext)
        JS_DestroyContextNoGC(mJSContext);
}

JSBool
ContextHolder::ContextHolderOperationCallback(JSContext *cx)
{
    ContextHolder* thisObject =
        static_cast<ContextHolder*>(JS_GetContextPrivate(cx));
    NS_ASSERTION(thisObject, "How did that happen?");

    JSContext *origCx = thisObject->mOrigCx;
    JSOperationCallback callback = JS_GetOperationCallback(origCx);
    JSBool ok = true;
    if (callback)
        ok = callback(origCx);
    return ok;
}

/***************************************************************************/

/* void evalInSandbox(in AString source, in nativeobj sandbox); */
NS_IMETHODIMP
nsXPCComponents_Utils::EvalInSandbox(const nsAString& source,
                                     const JS::Value& sandboxVal,
                                     const JS::Value& version,
                                     const JS::Value& filenameVal,
                                     PRInt32 lineNumber,
                                     JSContext *cx,
                                     PRUint8 optionalArgc,
                                     JS::Value *retval)
{
    JSObject *sandbox;
    if (!JS_ValueToObject(cx, sandboxVal, &sandbox) || !sandbox)
        return NS_ERROR_INVALID_ARG;

    // Optional third argument: JS version, as a string.
    JSVersion jsVersion = JSVERSION_DEFAULT;
    if (optionalArgc >= 1) {
        JSString *jsVersionStr = JS_ValueToString(cx, version);
        if (!jsVersionStr)
            return NS_ERROR_INVALID_ARG;

        JSAutoByteString bytes(cx, jsVersionStr);
        if (!bytes)
            return NS_ERROR_INVALID_ARG;

        jsVersion = JS_StringToVersion(bytes.ptr());
        if (jsVersion == JSVERSION_UNKNOWN)
            return NS_ERROR_INVALID_ARG;
    }

    // Optional fourth and fifth arguments: filename and line number.
    nsXPIDLCString filename;
    PRInt32 lineNo = (optionalArgc >= 3) ? lineNumber : 0;
    if (optionalArgc >= 2) {
        JSString *filenameStr = JS_ValueToString(cx, filenameVal);
        if (!filenameStr)
            return NS_ERROR_INVALID_ARG;

        JSAutoByteString filenameBytes;
        if (!filenameBytes.encode(cx, filenameStr))
            return NS_ERROR_INVALID_ARG;
        filename = filenameBytes.ptr();
    } else {
        // Get the current source info from xpc.
        nsresult rv;
        nsCOMPtr<nsIXPConnect> xpc = do_GetService(nsIXPConnect::GetCID(), &rv);
        NS_ENSURE_SUCCESS(rv, rv);

        nsCOMPtr<nsIStackFrame> frame;
        xpc->GetCurrentJSStack(getter_AddRefs(frame));
        if (frame) {
            frame->GetFilename(getter_Copies(filename));
            frame->GetLineNumber(&lineNo);
        }
    }

    return xpc_EvalInSandbox(cx, sandbox, source, filename.get(), lineNo,
                             jsVersion, false, retval);
}

nsresult
xpc_EvalInSandbox(JSContext *cx, JSObject *sandbox, const nsAString& source,
                  const char *filename, PRInt32 lineNo,
                  JSVersion jsVersion, bool returnStringOnly, jsval *rval)
{
    JS_AbortIfWrongThread(JS_GetRuntime(cx));

#ifdef DEBUG
    // NB: The "unsafe" unwrap here is OK because we must be called from chrome.
    {
        nsIScriptSecurityManager *ssm = XPCWrapper::GetSecurityManager();
        if (ssm) {
            JSStackFrame *fp;
            nsIPrincipal *subjectPrincipal =
                ssm->GetCxSubjectPrincipalAndFrame(cx, &fp);
            bool system;
            ssm->IsSystemPrincipal(subjectPrincipal, &system);
            if (fp && !system) {
                ssm->IsCapabilityEnabled("UniversalXPConnect", &system);
                NS_ASSERTION(system, "Bad caller!");
            }
        }
    }
#endif

    sandbox = XPCWrapper::UnsafeUnwrapSecurityWrapper(sandbox);
    if (!sandbox || js::GetObjectJSClass(sandbox) != &SandboxClass) {
        return NS_ERROR_INVALID_ARG;
    }

    nsIScriptObjectPrincipal *sop =
        (nsIScriptObjectPrincipal*)xpc_GetJSPrivate(sandbox);
    NS_ASSERTION(sop, "Invalid sandbox passed");
    nsCOMPtr<nsIPrincipal> prin = sop->GetPrincipal();

    if (!prin) {
        return NS_ERROR_FAILURE;
    }

    nsCAutoString filenameBuf;
    if (!filename) {
        // Default to the spec of the principal.
        nsJSPrincipals::get(prin)->GetScriptLocation(filenameBuf);
        filename = filenameBuf.get();
        lineNo = 1;
    }

    JSObject *callingScope;
    {
        JSAutoRequest req(cx);

        callingScope = JS_GetGlobalForScopeChain(cx);
        if (!callingScope) {
            return NS_ERROR_FAILURE;
        }
    }

    nsRefPtr<ContextHolder> sandcx = new ContextHolder(cx, sandbox);
    if (!sandcx || !sandcx->GetJSContext()) {
        JS_ReportError(cx, "Can't prepare context for evalInSandbox");
        return NS_ERROR_OUT_OF_MEMORY;
    }

    if (jsVersion != JSVERSION_DEFAULT)
        JS_SetVersion(sandcx->GetJSContext(), jsVersion);

    XPCPerThreadData *data = XPCPerThreadData::GetData(cx);
    XPCJSContextStack *stack = nsnull;
    if (data && (stack = data->GetJSContextStack())) {
        if (!stack->Push(sandcx->GetJSContext())) {
            JS_ReportError(cx,
                           "Unable to initialize XPConnect with the sandbox context");
            return NS_ERROR_FAILURE;
        }
    }

    nsresult rv = NS_OK;

    {
        JSAutoRequest req(sandcx->GetJSContext());
        JSAutoEnterCompartment ac;

        if (!ac.enter(sandcx->GetJSContext(), sandbox)) {
            if (stack)
                unused << stack->Pop();
            return NS_ERROR_FAILURE;
        }

        jsval v;
        JSString *str = nsnull;
        JSBool ok =
            JS_EvaluateUCScriptForPrincipals(sandcx->GetJSContext(), sandbox,
                                             nsJSPrincipals::get(prin),
                                             reinterpret_cast<const jschar *>
                                                             (PromiseFlatString(source).get()),
                                             source.Length(), filename, lineNo,
                                             &v);
        if (ok && returnStringOnly && !(JSVAL_IS_VOID(v))) {
            ok = !!(str = JS_ValueToString(sandcx->GetJSContext(), v));
        }

        if (!ok) {
            // The sandbox threw an exception, convert it to a string (if
            // asked) or convert it to a SJOW.

            jsval exn;
            if (JS_GetPendingException(sandcx->GetJSContext(), &exn)) {
                JS_ClearPendingException(sandcx->GetJSContext());

                if (returnStringOnly) {
                    // The caller asked for strings only, convert the
                    // exception into a string.
                    str = JS_ValueToString(sandcx->GetJSContext(), exn);

                    if (str) {
                        // We converted the exception to a string. Use that
                        // as the value exception.
                        exn = STRING_TO_JSVAL(str);
                        if (JS_WrapValue(cx, &exn)) {
                            JS_SetPendingException(cx, exn);
                        } else {
                            JS_ClearPendingException(cx);
                            rv = NS_ERROR_FAILURE;
                        }
                    } else {
                        JS_ClearPendingException(cx);
                        rv = NS_ERROR_FAILURE;
                    }
                } else {
                    if (JS_WrapValue(cx, &exn)) {
                        JS_SetPendingException(cx, exn);
                    }
                }


                // Clear str so we don't confuse callers.
                str = nsnull;
            } else {
                rv = NS_ERROR_OUT_OF_MEMORY;
            }
        } else {
            // Convert the result into something safe for our caller.
            JSAutoRequest req(cx);
            JSAutoEnterCompartment ac;
            if (str) {
                v = STRING_TO_JSVAL(str);
            }

            xpc::CompartmentPrivate *sandboxdata =
                static_cast<xpc::CompartmentPrivate *>
                           (JS_GetCompartmentPrivate(js::GetObjectCompartment(sandbox)));
            if (!ac.enter(cx, callingScope) ||
                !WrapForSandbox(cx, sandboxdata->wantXrays, &v)) {
                rv = NS_ERROR_FAILURE;
            }

            if (NS_SUCCEEDED(rv)) {
                *rval = v;
            }
        }
    }

    if (stack)
        unused << stack->Pop();

    return rv;
}

/* JSObject import (in AUTF8String registryLocation,
 *                  [optional] in JSObject targetObj);
 */
NS_IMETHODIMP
nsXPCComponents_Utils::Import(const nsACString& registryLocation,
                              const JS::Value& targetObj,
                              JSContext* cx,
                              PRUint8 optionalArgc,
                              JS::Value* retval)
{
    nsCOMPtr<xpcIJSModuleLoader> moduleloader =
        do_GetService(MOZJSCOMPONENTLOADER_CONTRACTID);
    if (!moduleloader)
        return NS_ERROR_FAILURE;
    return moduleloader->Import(registryLocation, targetObj, cx, optionalArgc, retval);
}

/* unload (in AUTF8String registryLocation);
 */
NS_IMETHODIMP
nsXPCComponents_Utils::Unload(const nsACString & registryLocation)
{
    nsCOMPtr<xpcIJSModuleLoader> moduleloader =
        do_GetService(MOZJSCOMPONENTLOADER_CONTRACTID);
    if (!moduleloader)
        return NS_ERROR_FAILURE;
    return moduleloader->Unload(registryLocation);
}

/* xpcIJSWeakReference getWeakReference (); */
NS_IMETHODIMP
nsXPCComponents_Utils::GetWeakReference(const JS::Value &object, JSContext *cx,
                                        xpcIJSWeakReference **_retval)
{
    nsRefPtr<xpcJSWeakReference> ref = new xpcJSWeakReference();
    nsresult rv = ref->Init(cx, object);
    NS_ENSURE_SUCCESS(rv, rv);
    ref.forget(_retval);
    return NS_OK;
}

/* void forceGC (); */
NS_IMETHODIMP
nsXPCComponents_Utils::ForceGC()
{
    JSRuntime* rt = nsXPConnect::GetRuntimeInstance()->GetJSRuntime();
    js::PrepareForFullGC(rt);
    js::GCForReason(rt, js::gcreason::COMPONENT_UTILS);
    return NS_OK;
}

/* void forceShrinkingGC (); */
NS_IMETHODIMP
nsXPCComponents_Utils::ForceShrinkingGC()
{
    JSRuntime* rt = nsXPConnect::GetRuntimeInstance()->GetJSRuntime();
    js::PrepareForFullGC(rt);
    js::ShrinkingGC(rt, js::gcreason::COMPONENT_UTILS);
    return NS_OK;
}

class PreciseGCRunnable : public nsRunnable
{
  public:
    PreciseGCRunnable(ScheduledGCCallback* aCallback, bool aShrinking)
    : mCallback(aCallback), mShrinking(aShrinking) {}

    NS_IMETHOD Run()
    {
        JSRuntime* rt = nsXPConnect::GetRuntimeInstance()->GetJSRuntime();

        JSContext *cx;
        JSContext *iter = nsnull;
        while ((cx = JS_ContextIterator(rt, &iter)) != NULL) {
            if (JS_IsRunning(cx)) {
                return NS_DispatchToMainThread(this);
            }
        }

        js::PrepareForFullGC(rt);
        if (mShrinking)
            js::ShrinkingGC(rt, js::gcreason::COMPONENT_UTILS);
        else
            js::GCForReason(rt, js::gcreason::COMPONENT_UTILS);

        mCallback->Callback();
        return NS_OK;
    }

  private:
    nsRefPtr<ScheduledGCCallback> mCallback;
    bool mShrinking;
};

/* void schedulePreciseGC(in ScheduledGCCallback callback); */
NS_IMETHODIMP
nsXPCComponents_Utils::SchedulePreciseGC(ScheduledGCCallback* aCallback)
{
    nsRefPtr<PreciseGCRunnable> event = new PreciseGCRunnable(aCallback, false);
    return NS_DispatchToMainThread(event);
}

/* void schedulePreciseShrinkingGC(in ScheduledGCCallback callback); */
NS_IMETHODIMP
nsXPCComponents_Utils::SchedulePreciseShrinkingGC(ScheduledGCCallback* aCallback)
{
    nsRefPtr<PreciseGCRunnable> event = new PreciseGCRunnable(aCallback, true);
    return NS_DispatchToMainThread(event);
}

/* [implicit_jscontext] jsval nondeterministicGetWeakMapKeys(in jsval aMap); */
NS_IMETHODIMP
nsXPCComponents_Utils::NondeterministicGetWeakMapKeys(const jsval &aMap,
                                                      JSContext *aCx,
                                                      jsval *aKeys)
{
    if (!JSVAL_IS_OBJECT(aMap)) {
        *aKeys = JSVAL_VOID;
        return NS_OK; 
    }
    JSObject *objRet;
    if (!JS_NondeterministicGetWeakMapKeys(aCx, JSVAL_TO_OBJECT(aMap), &objRet))
        return NS_ERROR_OUT_OF_MEMORY;
    *aKeys = objRet ? OBJECT_TO_JSVAL(objRet) : JSVAL_VOID;
    return NS_OK;
}

/* void getDebugObject(); */
NS_IMETHODIMP
nsXPCComponents_Utils::GetJSTestingFunctions(JSContext *cx,
                                             JS::Value *retval)
{
    JSObject *obj = js::GetTestingFunctions(cx);
    if (!obj)
        return NS_ERROR_XPC_JAVASCRIPT_ERROR;
    *retval = OBJECT_TO_JSVAL(obj);
    return NS_OK;
}

/* void getGlobalForObject(); */
NS_IMETHODIMP
nsXPCComponents_Utils::GetGlobalForObject(const JS::Value& object,
                                          JSContext *cx,
                                          JS::Value *retval)
{
  // First argument must be an object.
  if (JSVAL_IS_PRIMITIVE(object))
    return NS_ERROR_XPC_BAD_CONVERT_JS;

  // Wrappers are parented to their the global in their home compartment. But
  // when getting the global for a cross-compartment wrapper, we really want
  // a wrapper for the foreign global. So we need to unwrap before getting the
  // parent, enter the compartment for the duration of the call, and wrap the
  // result.
  JSObject *obj = JSVAL_TO_OBJECT(object);
  obj = js::UnwrapObject(obj);
  {
    JSAutoEnterCompartment ac;
    if (!ac.enter(cx, obj))
      return NS_ERROR_FAILURE;
    obj = JS_GetGlobalForObject(cx, obj);
  }
  JS_WrapObject(cx, &obj);
  *retval = OBJECT_TO_JSVAL(obj);

  // Outerize if necessary.
  if (JSObjectOp outerize = js::GetObjectClass(obj)->ext.outerObject)
      *retval = OBJECT_TO_JSVAL(outerize(cx, obj));

  return NS_OK;
}

/* jsval createObjectIn(in jsval vobj); */
NS_IMETHODIMP
nsXPCComponents_Utils::CreateObjectIn(const jsval &vobj, JSContext *cx, jsval *rval)
{
    if (!cx)
        return NS_ERROR_FAILURE;

    // first argument must be an object
    if (JSVAL_IS_PRIMITIVE(vobj))
        return NS_ERROR_XPC_BAD_CONVERT_JS;

    JSObject *scope = js::UnwrapObject(JSVAL_TO_OBJECT(vobj));
    JSObject *obj;
    {
        JSAutoEnterCompartment ac;
        if (!ac.enter(cx, scope))
            return NS_ERROR_FAILURE;

        obj = JS_NewObject(cx, nsnull, nsnull, scope);
        if (!obj)
            return NS_ERROR_FAILURE;
    }

    if (!JS_WrapObject(cx, &obj))
        return NS_ERROR_FAILURE;
    *rval = OBJECT_TO_JSVAL(obj);
    return NS_OK;
}

JSBool
FunctionWrapper(JSContext *cx, unsigned argc, jsval *vp)
{
    jsval v = js::GetFunctionNativeReserved(JSVAL_TO_OBJECT(JS_CALLEE(cx, vp)), 0);
    NS_ASSERTION(JSVAL_IS_OBJECT(v), "weird function");

    JSObject *obj = JS_THIS_OBJECT(cx, vp);
    if (!obj) {
        return JS_FALSE;
    }
    return JS_CallFunctionValue(cx, obj, v, argc, JS_ARGV(cx, vp), vp);
}

JSBool
WrapCallable(JSContext *cx, JSObject *obj, jsid id, JSObject *propobj, jsval *vp)
{
    JSFunction *fun = js::NewFunctionByIdWithReserved(cx, FunctionWrapper, 0, 0,
                                                      JS_GetGlobalForObject(cx, obj), id);
    if (!fun)
        return false;

    JSObject *funobj = JS_GetFunctionObject(fun);
    js::SetFunctionNativeReserved(funobj, 0, OBJECT_TO_JSVAL(propobj));
    *vp = OBJECT_TO_JSVAL(funobj);
    return true;
}

/* void makeObjectPropsNormal(jsval vobj); */
NS_IMETHODIMP
nsXPCComponents_Utils::MakeObjectPropsNormal(const jsval &vobj, JSContext *cx)
{
    if (!cx)
        return NS_ERROR_FAILURE;

    // first argument must be an object
    if (JSVAL_IS_PRIMITIVE(vobj))
        return NS_ERROR_XPC_BAD_CONVERT_JS;

    JSObject *obj = js::UnwrapObject(JSVAL_TO_OBJECT(vobj));

    JSAutoEnterCompartment ac;
    if (!ac.enter(cx, obj))
        return NS_ERROR_FAILURE;

    JS::AutoIdArray ida(cx, JS_Enumerate(cx, obj));
    if (!ida)
        return NS_ERROR_FAILURE;

    for (size_t i = 0; i < ida.length(); ++i) {
        jsid id = ida[i];
        jsval v;

        if (!JS_GetPropertyById(cx, obj, id, &v))
            return NS_ERROR_FAILURE;

        if (JSVAL_IS_PRIMITIVE(v))
            continue;

        JSObject *propobj = JSVAL_TO_OBJECT(v);
        // TODO Deal with non-functions.
        if (!js::IsWrapper(propobj) || !JS_ObjectIsCallable(cx, propobj))
            continue;

        if (!WrapCallable(cx, obj, id, propobj, &v) ||
            !JS_SetPropertyById(cx, obj, id, &v))
            return NS_ERROR_FAILURE;
    }

    return NS_OK;
}

/* string canCreateWrapper (in nsIIDPtr iid); */
NS_IMETHODIMP
nsXPCComponents_Utils::CanCreateWrapper(const nsIID * iid, char **_retval)
{
    // We let anyone do this...
    *_retval = xpc_CloneAllAccess();
    return NS_OK;
}

/* string canCallMethod (in nsIIDPtr iid, in wstring methodName); */
NS_IMETHODIMP
nsXPCComponents_Utils::CanCallMethod(const nsIID * iid, const PRUnichar *methodName, char **_retval)
{
    static const char* allowed[] = { "lookupMethod", "evalInSandbox", nsnull };
    *_retval = xpc_CheckAccessList(methodName, allowed);
    return NS_OK;
}

/* string canGetProperty (in nsIIDPtr iid, in wstring propertyName); */
NS_IMETHODIMP
nsXPCComponents_Utils::CanGetProperty(const nsIID * iid, const PRUnichar *propertyName, char **_retval)
{
    *_retval = nsnull;
    return NS_OK;
}

/* string canSetProperty (in nsIIDPtr iid, in wstring propertyName); */
NS_IMETHODIMP
nsXPCComponents_Utils::CanSetProperty(const nsIID * iid, const PRUnichar *propertyName, char **_retval)
{
    // If you have to ask, then the answer is NO
    *_retval = nsnull;
    return NS_OK;
}

nsresult
GetBoolOption(JSContext* cx, uint32_t aOption, bool* aValue)
{
    *aValue = !!(JS_GetOptions(cx) & aOption);
    return NS_OK;
}

nsresult
SetBoolOption(JSContext* cx, uint32_t aOption, bool aValue)
{
    uint32_t options = JS_GetOptions(cx);
    if (aValue) {
        options |= aOption;
    } else {
        options &= ~aOption;
    }
    JS_SetOptions(cx, options & JSALLOPTION_MASK);
    return NS_OK;
}

#define GENERATE_JSOPTION_GETTER_SETTER(_attr, _flag)                   \
    NS_IMETHODIMP                                                       \
    nsXPCComponents_Utils::Get## _attr(JSContext* cx, bool* aValue)     \
    {                                                                   \
        return GetBoolOption(cx, _flag, aValue);                        \
    }                                                                   \
    NS_IMETHODIMP                                                       \
    nsXPCComponents_Utils::Set## _attr(JSContext* cx, bool aValue)      \
    {                                                                   \
        return SetBoolOption(cx, _flag, aValue);                        \
    }

GENERATE_JSOPTION_GETTER_SETTER(Strict, JSOPTION_STRICT)
GENERATE_JSOPTION_GETTER_SETTER(Werror, JSOPTION_WERROR)
GENERATE_JSOPTION_GETTER_SETTER(Atline, JSOPTION_ATLINE)
GENERATE_JSOPTION_GETTER_SETTER(Xml, JSOPTION_XML)
GENERATE_JSOPTION_GETTER_SETTER(Relimit, JSOPTION_RELIMIT)
GENERATE_JSOPTION_GETTER_SETTER(Methodjit, JSOPTION_METHODJIT)
GENERATE_JSOPTION_GETTER_SETTER(Methodjit_always, JSOPTION_METHODJIT_ALWAYS)

#undef GENERATE_JSOPTION_GETTER_SETTER

NS_IMETHODIMP
nsXPCComponents_Utils::SetGCZeal(PRInt32 aValue, JSContext* cx)
{
#ifdef JS_GC_ZEAL
    JS_SetGCZeal(cx, PRUint8(aValue), JS_DEFAULT_ZEAL_FREQ);
#endif
    return NS_OK;
}

/***************************************************************************/
/***************************************************************************/
/***************************************************************************/

// XXXjband We ought to cache the wrapper in the object's slots rather than
// re-wrapping on demand

NS_INTERFACE_MAP_BEGIN(nsXPCComponents)
  NS_INTERFACE_MAP_ENTRY(nsIXPCComponents)
  NS_INTERFACE_MAP_ENTRY(nsIXPCScriptable)
  NS_INTERFACE_MAP_ENTRY(nsIClassInfo)
  NS_INTERFACE_MAP_ENTRY(nsISecurityCheckedComponent)
  NS_INTERFACE_MAP_ENTRY_AMBIGUOUS(nsISupports, nsIXPCComponents)
NS_INTERFACE_MAP_END_THREADSAFE

NS_IMPL_THREADSAFE_ADDREF(nsXPCComponents)
NS_IMPL_THREADSAFE_RELEASE(nsXPCComponents)

/* void getInterfaces (out PRUint32 count, [array, size_is (count), retval]
                       out nsIIDPtr array); */
NS_IMETHODIMP
nsXPCComponents::GetInterfaces(PRUint32 *aCount, nsIID * **aArray)
{
    const PRUint32 count = 3;
    *aCount = count;
    nsIID **array;
    *aArray = array = static_cast<nsIID**>(nsMemory::Alloc(count * sizeof(nsIID*)));
    if (!array)
        return NS_ERROR_OUT_OF_MEMORY;

    PRUint32 index = 0;
    nsIID* clone;
#define PUSH_IID(id)                                                          \
    clone = static_cast<nsIID *>(nsMemory::Clone(&NS_GET_IID( id ),           \
                                                 sizeof(nsIID)));             \
    if (!clone)                                                               \
        goto oom;                                                             \
    array[index++] = clone;

    PUSH_IID(nsIXPCComponents)
    PUSH_IID(nsIXPCScriptable)
    PUSH_IID(nsISecurityCheckedComponent)
#undef PUSH_IID

    return NS_OK;
oom:
    while (index)
        nsMemory::Free(array[--index]);
    nsMemory::Free(array);
    *aArray = nsnull;
    return NS_ERROR_OUT_OF_MEMORY;
}

/* nsISupports getHelperForLanguage (in PRUint32 language); */
NS_IMETHODIMP
nsXPCComponents::GetHelperForLanguage(PRUint32 language,
                                      nsISupports **retval)
{
    *retval = static_cast<nsIXPCComponents*>(this);
    NS_ADDREF(this);
    return NS_OK;
}

/* readonly attribute string contractID; */
NS_IMETHODIMP
nsXPCComponents::GetContractID(char * *aContractID)
{
    *aContractID = nsnull;
    return NS_ERROR_NOT_AVAILABLE;
}

/* readonly attribute string classDescription; */
NS_IMETHODIMP
nsXPCComponents::GetClassDescription(char * *aClassDescription)
{
    static const char classDescription[] = "XPCComponents";
    *aClassDescription = (char*)nsMemory::Clone(classDescription, sizeof(classDescription));
    return *aClassDescription ? NS_OK : NS_ERROR_OUT_OF_MEMORY;
}

/* readonly attribute nsCIDPtr classID; */
NS_IMETHODIMP
nsXPCComponents::GetClassID(nsCID * *aClassID)
{
    *aClassID = nsnull;
    return NS_OK;
}

/* readonly attribute PRUint32 implementationLanguage; */
NS_IMETHODIMP
nsXPCComponents::GetImplementationLanguage(PRUint32 *aImplementationLanguage)
{
    *aImplementationLanguage = nsIProgrammingLanguage::CPLUSPLUS;
    return NS_OK;
}

/* readonly attribute PRUint32 flags; */
NS_IMETHODIMP
nsXPCComponents::GetFlags(PRUint32 *aFlags)
{
    *aFlags = nsIClassInfo::THREADSAFE;
    return NS_OK;
}

/* [notxpcom] readonly attribute nsCID classIDNoAlloc; */
NS_IMETHODIMP
nsXPCComponents::GetClassIDNoAlloc(nsCID *aClassIDNoAlloc)
{
    return NS_ERROR_NOT_AVAILABLE;
}

nsXPCComponents::nsXPCComponents(XPCWrappedNativeScope* aScope)
    :   mScope(aScope),
        mInterfaces(nsnull),
        mInterfacesByID(nsnull),
        mClasses(nsnull),
        mClassesByID(nsnull),
        mResults(nsnull),
        mID(nsnull),
        mException(nsnull),
        mConstructor(nsnull),
        mUtils(nsnull)
{
    MOZ_ASSERT(aScope, "aScope must not be null");
}

nsXPCComponents::~nsXPCComponents()
{
    ClearMembers();
}

void
nsXPCComponents::ClearMembers()
{
    NS_IF_RELEASE(mInterfaces);
    NS_IF_RELEASE(mInterfacesByID);
    NS_IF_RELEASE(mClasses);
    NS_IF_RELEASE(mClassesByID);
    NS_IF_RELEASE(mResults);
    NS_IF_RELEASE(mID);
    NS_IF_RELEASE(mException);
    NS_IF_RELEASE(mConstructor);
    NS_IF_RELEASE(mUtils);
}

/*******************************************/
#define XPC_IMPL_GET_OBJ_METHOD(_n)                                           \
NS_IMETHODIMP nsXPCComponents::Get##_n(nsIXPCComponents_##_n * *a##_n) {      \
    NS_ENSURE_ARG_POINTER(a##_n);                                             \
    if (!m##_n) {                                                             \
        if (!(m##_n = new nsXPCComponents_##_n())) {                          \
            *a##_n = nsnull;                                                  \
            return NS_ERROR_OUT_OF_MEMORY;                                    \
        }                                                                     \
        NS_ADDREF(m##_n);                                                     \
    }                                                                         \
    NS_ADDREF(m##_n);                                                         \
    *a##_n = m##_n;                                                           \
    return NS_OK;                                                             \
}

XPC_IMPL_GET_OBJ_METHOD(Interfaces)
XPC_IMPL_GET_OBJ_METHOD(InterfacesByID)
XPC_IMPL_GET_OBJ_METHOD(Classes)
XPC_IMPL_GET_OBJ_METHOD(ClassesByID)
XPC_IMPL_GET_OBJ_METHOD(Results)
XPC_IMPL_GET_OBJ_METHOD(ID)
XPC_IMPL_GET_OBJ_METHOD(Exception)
XPC_IMPL_GET_OBJ_METHOD(Constructor)
XPC_IMPL_GET_OBJ_METHOD(Utils)

#undef XPC_IMPL_GET_OBJ_METHOD
/*******************************************/

NS_IMETHODIMP
nsXPCComponents::IsSuccessCode(nsresult result, bool *out)
{
    *out = NS_SUCCEEDED(result);
    return NS_OK;
}

NS_IMETHODIMP
nsXPCComponents::GetStack(nsIStackFrame * *aStack)
{
    nsresult rv;
    nsXPConnect* xpc = nsXPConnect::GetXPConnect();
    if (!xpc)
        return NS_ERROR_FAILURE;
    rv = xpc->GetCurrentJSStack(aStack);
    return rv;
}

NS_IMETHODIMP
nsXPCComponents::GetManager(nsIComponentManager * *aManager)
{
    NS_ASSERTION(aManager, "bad param");
    return NS_GetComponentManager(aManager);
}

/**********************************************/

// The nsIXPCScriptable map declaration that will generate stubs for us...
#define XPC_MAP_CLASSNAME           nsXPCComponents
#define XPC_MAP_QUOTED_CLASSNAME   "nsXPCComponents"
#define                             XPC_MAP_WANT_NEWRESOLVE
#define                             XPC_MAP_WANT_GETPROPERTY
#define                             XPC_MAP_WANT_SETPROPERTY
#define                             XPC_MAP_WANT_PRECREATE
#define XPC_MAP_FLAGS               nsIXPCScriptable::ALLOW_PROP_MODS_DURING_RESOLVE
#include "xpc_map_end.h" /* This will #undef the above */

/* bool newResolve (in nsIXPConnectWrappedNative wrapper, in JSContextPtr cx, in JSObjectPtr obj, in jsval id, in PRUint32 flags, out JSObjectPtr objp); */
NS_IMETHODIMP
nsXPCComponents::NewResolve(nsIXPConnectWrappedNative *wrapper,
                            JSContext * cx, JSObject * obj,
                            jsid id, PRUint32 flags,
                            JSObject * *objp, bool *_retval)
{
    XPCJSRuntime* rt = nsXPConnect::GetRuntimeInstance();
    if (!rt)
        return NS_ERROR_FAILURE;

    unsigned attrs = 0;

    if (id == rt->GetStringID(XPCJSRuntime::IDX_LAST_RESULT))
        attrs = JSPROP_READONLY;
    else if (id != rt->GetStringID(XPCJSRuntime::IDX_RETURN_CODE))
        return NS_OK;

    *objp = obj;
    *_retval = JS_DefinePropertyById(cx, obj, id, JSVAL_VOID, nsnull, nsnull,
                                     JSPROP_ENUMERATE | JSPROP_PERMANENT |
                                     attrs);
    return NS_OK;
}

/* bool getProperty (in nsIXPConnectWrappedNative wrapper, in JSContextPtr cx, in JSObjectPtr obj, in jsval id, in JSValPtr vp); */
NS_IMETHODIMP
nsXPCComponents::GetProperty(nsIXPConnectWrappedNative *wrapper,
                             JSContext * cx, JSObject * obj,
                             jsid id, jsval * vp, bool *_retval)
{
    XPCContext* xpcc = XPCContext::GetXPCContext(cx);
    if (!xpcc)
        return NS_ERROR_FAILURE;

    bool doResult = false;
    nsresult res;
    XPCJSRuntime* rt = xpcc->GetRuntime();
    if (id == rt->GetStringID(XPCJSRuntime::IDX_LAST_RESULT)) {
        res = xpcc->GetLastResult();
        doResult = true;
    } else if (id == rt->GetStringID(XPCJSRuntime::IDX_RETURN_CODE)) {
        res = xpcc->GetPendingResult();
        doResult = true;
    }

    nsresult rv = NS_OK;
    if (doResult) {
        if (!JS_NewNumberValue(cx, (double) res, vp))
            return NS_ERROR_OUT_OF_MEMORY;
        rv = NS_SUCCESS_I_DID_SOMETHING;
    }

    return rv;
}

/* bool setProperty (in nsIXPConnectWrappedNative wrapper, in JSContextPtr cx, in JSObjectPtr obj, in jsid id, in JSValPtr vp); */
NS_IMETHODIMP
nsXPCComponents::SetProperty(nsIXPConnectWrappedNative *wrapper,
                             JSContext * cx, JSObject * obj, jsid id,
                             jsval * vp, bool *_retval)
{
    XPCContext* xpcc = XPCContext::GetXPCContext(cx);
    if (!xpcc)
        return NS_ERROR_FAILURE;

    XPCJSRuntime* rt = xpcc->GetRuntime();
    if (!rt)
        return NS_ERROR_FAILURE;

    if (id == rt->GetStringID(XPCJSRuntime::IDX_RETURN_CODE)) {
        nsresult rv;
        if (JS_ValueToECMAUint32(cx, *vp, (uint32_t*)&rv)) {
            xpcc->SetPendingResult(rv);
            xpcc->SetLastResult(rv);
            return NS_SUCCESS_I_DID_SOMETHING;
        }
        return NS_ERROR_FAILURE;
    }

    return NS_ERROR_XPC_CANT_MODIFY_PROP_ON_WN;
}

// static
JSBool
nsXPCComponents::AttachComponentsObject(XPCCallContext& ccx,
                                        XPCWrappedNativeScope* aScope,
                                        JSObject* aGlobal)
{
    if (!aGlobal)
        return false;

    nsXPCComponents* components = aScope->GetComponents();
    if (!components) {
        components = new nsXPCComponents(aScope);
        if (!components)
            return false;
        aScope->SetComponents(components);
    }

    nsCOMPtr<nsIXPCComponents> cholder(components);

    AutoMarkingNativeInterfacePtr iface(ccx);
    iface = XPCNativeInterface::GetNewOrUsed(ccx, &NS_GET_IID(nsIXPCComponents));

    if (!iface)
        return false;

    nsCOMPtr<XPCWrappedNative> wrapper;
    xpcObjectHelper helper(cholder);
    XPCWrappedNative::GetNewOrUsed(ccx, helper, aScope, iface, getter_AddRefs(wrapper));
    if (!wrapper)
        return false;

    jsid id = ccx.GetRuntime()->GetStringID(XPCJSRuntime::IDX_COMPONENTS);
    JSObject* obj = wrapper->GetSameCompartmentSecurityWrapper(ccx);
    if (!wrapper)
        return false;
   
    return JS_DefinePropertyById(ccx, aGlobal, id, OBJECT_TO_JSVAL(obj),
                                 nsnull, nsnull,
                                 JSPROP_PERMANENT | JSPROP_READONLY);
}

/* void lookupMethod (); */
NS_IMETHODIMP
nsXPCComponents::LookupMethod(const JS::Value& object,
                              const JS::Value& name,
                              JSContext *cx,
                              JS::Value *retval)
{
    NS_WARNING("Components.lookupMethod deprecated, use Components.utils.lookupMethod");

    nsCOMPtr<nsIXPCComponents_Utils> utils;
    nsresult rv = GetUtils(getter_AddRefs(utils));
    if (NS_FAILED(rv))
        return rv;

    return utils->LookupMethod(object, name, cx, retval);
}

/* void reportError (); */
NS_IMETHODIMP nsXPCComponents::ReportError(const JS::Value &error, JSContext *cx)
{
    NS_WARNING("Components.reportError deprecated, use Components.utils.reportError");

    nsCOMPtr<nsIXPCComponents_Utils> utils;
    nsresult rv = GetUtils(getter_AddRefs(utils));
    if (NS_FAILED(rv))
        return rv;

    return utils->ReportError(error, cx);
}

/* string canCreateWrapper (in nsIIDPtr iid); */
NS_IMETHODIMP
nsXPCComponents::CanCreateWrapper(const nsIID * iid, char **_retval)
{
    // We let anyone do this...
    *_retval = xpc_CloneAllAccess();
    return NS_OK;
}

/* string canCallMethod (in nsIIDPtr iid, in wstring methodName); */
NS_IMETHODIMP
nsXPCComponents::CanCallMethod(const nsIID * iid, const PRUnichar *methodName, char **_retval)
{
    static const char* allowed[] = { "isSuccessCode", "lookupMethod", nsnull };
    *_retval = xpc_CheckAccessList(methodName, allowed);
    return NS_OK;
}

/* string canGetProperty (in nsIIDPtr iid, in wstring propertyName); */
NS_IMETHODIMP
nsXPCComponents::CanGetProperty(const nsIID * iid, const PRUnichar *propertyName, char **_retval)
{
    static const char* allowed[] = { "interfaces", "interfacesByID", "results", nsnull};
    *_retval = xpc_CheckAccessList(propertyName, allowed);
    return NS_OK;
}

/* string canSetProperty (in nsIIDPtr iid, in wstring propertyName); */
NS_IMETHODIMP
nsXPCComponents::CanSetProperty(const nsIID * iid, const PRUnichar *propertyName, char **_retval)
{
    // If you have to ask, then the answer is NO
    *_retval = nsnull;
    return NS_OK;
}

NS_IMETHODIMP
nsXPCComponents::PreCreate(nsISupports *nativeObj, JSContext *cx, JSObject *globalObj, JSObject **parentObj)
{
  // this should never happen
  if (!mScope) {
      NS_WARNING("mScope must not be null when nsXPCComponents::PreCreate is called");
      return NS_ERROR_FAILURE;
  }
  *parentObj = mScope->GetGlobalJSObject();
  return NS_OK;
}