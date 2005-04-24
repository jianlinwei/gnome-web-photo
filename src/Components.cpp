/*
 *  Copyright (C) 2005 Christian Persch
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2, or (at your option)
 *  any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 *  $Id$
 */

#include "mozilla-config.h"
#include "config.h"

#include "Components.h"

#include <nsCOMPtr.h>
#include <nsIServiceManager.h>
#include <nsIComponentRegistrar.h>
#include <nsIGenericFactory.h>
#include <nsMemory.h>
#include <nsEmbedCID.h>
#include <stdio.h>

#define PROMPTER_CLASSNAME	"DummyPrompter"
#define PROMPTER_CID		{ 0x228965b9, 0x95d5, 0x4ae2, {0xa6, 0x88, 0x6e, 0x1d, 0x34, 0xc7, 0x83, 0xab} }

NS_IMPL_ISUPPORTS1(Prompter, nsIPromptService)

/* void alert (in nsIDOMWindow aParent, in wstring aDialogTitle, in wstring aText); */
NS_IMETHODIMP Prompter::Alert(nsIDOMWindow *aParent, const PRUnichar *aDialogTitle, const PRUnichar *aText)
{
  return NS_OK;
}

/* void alertCheck (in nsIDOMWindow aParent, in wstring aDialogTitle, in wstring aText, in wstring aCheckMsg, inout boolean aCheckState); */
NS_IMETHODIMP Prompter::AlertCheck(nsIDOMWindow *aParent, const PRUnichar *aDialogTitle, const PRUnichar *aText, const PRUnichar *aCheckMsg, PRBool *aCheckState)
{
  *aCheckState = PR_FALSE;
  return NS_OK;
}

/* boolean confirm (in nsIDOMWindow aParent, in wstring aDialogTitle, in wstring aText); */
NS_IMETHODIMP Prompter::Confirm(nsIDOMWindow *aParent, const PRUnichar *aDialogTitle, const PRUnichar *aText, PRBool *_retval)
{
  *_retval = PR_FALSE;
  return NS_OK;
}

/* boolean confirmCheck (in nsIDOMWindow aParent, in wstring aDialogTitle, in wstring aText, in wstring aCheckMsg, inout boolean aCheckState); */
NS_IMETHODIMP Prompter::ConfirmCheck(nsIDOMWindow *aParent, const PRUnichar *aDialogTitle, const PRUnichar *aText, const PRUnichar *aCheckMsg, PRBool *aCheckState, PRBool *_retval)
{
  *aCheckState = PR_FALSE;
  *_retval = PR_FALSE;
  return NS_OK;
}

/* PRInt32 confirmEx (in nsIDOMWindow aParent, in wstring aDialogTitle, in wstring aText, in unsigned long aButtonFlags, in wstring aButton0Title, in wstring aButton1Title, in wstring aButton2Title, in wstring aCheckMsg, inout boolean aCheckState); */
NS_IMETHODIMP Prompter::ConfirmEx(nsIDOMWindow *aParent, const PRUnichar *aDialogTitle, const PRUnichar *aText, PRUint32 aButtonFlags, const PRUnichar *aButton0Title, const PRUnichar *aButton1Title, const PRUnichar *aButton2Title, const PRUnichar *aCheckMsg, PRBool *aCheckState, PRInt32 *_retval)
{
#define FLAGS (BUTTON_TITLE_CANCEL | BUTTON_TITLE_NO | BUTTON_TITLE_DONT_SAVE)
  *aCheckState = PR_FALSE;
  *_retval = (aButtonFlags & FLAGS) ? 0 :
             (aButtonFlags & FLAGS * BUTTON_POS_1) ? 1 :
             (aButtonFlags & FLAGS * BUTTON_POS_2) ? 2 : -1;
             
  return NS_OK;
}

/* boolean prompt (in nsIDOMWindow aParent, in wstring aDialogTitle, in wstring aText, inout wstring aValue, in wstring aCheckMsg, inout boolean aCheckState); */
NS_IMETHODIMP Prompter::Prompt(nsIDOMWindow *aParent, const PRUnichar *aDialogTitle, const PRUnichar *aText, PRUnichar **aValue, const PRUnichar *aCheckMsg, PRBool *aCheckState, PRBool *_retval)
{
  *aCheckState = PR_FALSE;
  *_retval = PR_FALSE;
  return NS_OK;
}

/* boolean promptUsernameAndPassword (in nsIDOMWindow aParent, in wstring aDialogTitle, in wstring aText, inout wstring aUsername, inout wstring aPassword, in wstring aCheckMsg, inout boolean aCheckState); */
NS_IMETHODIMP Prompter::PromptUsernameAndPassword(nsIDOMWindow *aParent, const PRUnichar *aDialogTitle, const PRUnichar *aText, PRUnichar **aUsername, PRUnichar **aPassword, const PRUnichar *aCheckMsg, PRBool *aCheckState, PRBool *_retval)
{
  *aUsername = nsnull;
  *aPassword = nsnull;
  *_retval = PR_FALSE;
  return NS_OK;
}

/* boolean promptPassword (in nsIDOMWindow aParent, in wstring aDialogTitle, in wstring aText, inout wstring aPassword, in wstring aCheckMsg, inout boolean aCheckState); */
NS_IMETHODIMP Prompter::PromptPassword(nsIDOMWindow *aParent, const PRUnichar *aDialogTitle, const PRUnichar *aText, PRUnichar **aPassword, const PRUnichar *aCheckMsg, PRBool *aCheckState, PRBool *_retval)
{
  *aPassword = nsnull;
  *_retval = PR_FALSE;
  return NS_OK;
}

/* boolean select (in nsIDOMWindow aParent, in wstring aDialogTitle, in wstring aText, in PRUint32 aCount, [array, size_is (aCount)] in wstring aSelectList, out long aOutSelection); */
NS_IMETHODIMP Prompter::Select(nsIDOMWindow *aParent, const PRUnichar *aDialogTitle, const PRUnichar *aText, PRUint32 aCount, const PRUnichar **aSelectList, PRInt32 *aOutSelection, PRBool *_retval)
{
  *aOutSelection = 0;
  *_retval = PR_FALSE;
  return NS_OK;
}

/* -------------------------------------------------------------------------- */

NS_GENERIC_FACTORY_CONSTRUCTOR(Prompter)

static const nsModuleComponentInfo sAppComps[] =
{
  {
    PROMPTER_CLASSNAME,
    PROMPTER_CID,
    NS_PROMPTSERVICE_CONTRACTID,
    PrompterConstructor
  }
};

PRBool
RegisterComponents ()
{
  PRBool retval = PR_FALSE;
  nsresult rv;
  nsCOMPtr<nsIComponentRegistrar> cr;
  rv = NS_GetComponentRegistrar(getter_AddRefs(cr));
  NS_ENSURE_SUCCESS (rv, retval);

  retval = PR_TRUE;

  for (PRUint32 i = 0; i < NS_ARRAY_LENGTH(sAppComps); ++i) {
    nsCOMPtr<nsIGenericFactory> componentFactory;
    rv = NS_NewGenericFactory(getter_AddRefs(componentFactory), &(sAppComps[i]));
    if (NS_FAILED(rv) || !componentFactory) {
      printf ("Failed to make a factory for %s\n", sAppComps[i].mDescription);
      retval = PR_FALSE;
      continue;  /* don't abort registering other components */
    }

    rv = cr->RegisterFactory(sAppComps[i].mCID,
                             sAppComps[i].mDescription,
                             sAppComps[i].mContractID,
                             componentFactory);
    if (NS_FAILED(rv)) {
      printf ("Failed to register %s\n", sAppComps[i].mDescription);
      retval = PR_FALSE;
    }
  }

  return retval;
}