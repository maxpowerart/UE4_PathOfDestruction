// Copyright Csaba Molnar, Daniel Butum. All Rights Reserved.
#include "DialogueK2Node_SwitchDialogueCallback.h"

#include "BlueprintNodeSpawner.h"
#include "BlueprintActionDatabaseRegistrar.h"
#include "EdGraphSchema_K2.h"
#include "Kismet/KismetMathLibrary.h"
#include "UObject/UObjectIterator.h"

#include "DlgSystemEditorModule.h"
#include "DlgManager.h"
#include "DialogueBlueprintUtilities.h"

#define LOCTEXT_NAMESPACE "DlgK2Node_Select"

UDialogueK2Node_SwitchDialogueCallback::UDialogueK2Node_SwitchDialogueCallback(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	FunctionName = TEXT("NotEqual_NameName");
	FunctionClass = UKismetMathLibrary::StaticClass();
	bHasDefaultPin = true;
	AdvancedPinDisplay = ENodeAdvancedPins::NoPins;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Begin UEdGraphNode interface
void UDialogueK2Node_SwitchDialogueCallback::AllocateDefaultPins()
{
	RefreshPinNames();
	Super::AllocateDefaultPins();
}

FText UDialogueK2Node_SwitchDialogueCallback::GetNodeTitle(ENodeTitleType::Type TitleType) const
{
	return LOCTEXT("DlgCallbackSwitch_NodeTitle", "Switch on Relevant Dialogue Callback");
}

FText UDialogueK2Node_SwitchDialogueCallback::GetTooltipText() const
{
	return LOCTEXT("DlgCallbackSwitch_Tooltip", "Selects an output based on the input");
}
// End UEdGraphNode interface
////////////////////////////////////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Begin UK2Node Interface
void UDialogueK2Node_SwitchDialogueCallback::GetMenuActions(FBlueprintActionDatabaseRegistrar& ActionRegistrar) const
{
	// actions get registered under specific object-keys; the idea is that
	// actions might have to be updated (or deleted) if their object-key is
	// mutated (or removed)... here we use the node's class (so if the node
	// type disappears, then the action should go with it)
	UClass* ActionKey = GetClass();

	// to keep from needlessly instantiating a UBlueprintNodeSpawner, first
	// check to make sure that the registrar is looking for actions of this type
	// (could be regenerating actions for a specific asset, and therefore the
	// registrar would only accept actions corresponding to that asset)
	if (ActionRegistrar.IsOpenForRegistration(ActionKey))
	{
		UBlueprintNodeSpawner* NodeSpawner = UBlueprintNodeSpawner::Create(GetClass());
		check(NodeSpawner);

		ActionRegistrar.AddBlueprintAction(ActionKey, NodeSpawner);
	}
}

FText UDialogueK2Node_SwitchDialogueCallback::GetMenuCategory() const
{
	return LOCTEXT("DlgCallbackSwitch_MenuCategory", "Dialogue|Switch");
}

void UDialogueK2Node_SwitchDialogueCallback::CreateSelectionPin()
{
	const UEdGraphSchema_K2* K2Schema = GetDefault<UEdGraphSchema_K2>();
	UEdGraphPin* Pin = CreatePin(EGPD_Input, K2Schema->PC_Name, GetSelectionPinName());
	K2Schema->SetPinAutogeneratedDefaultValueBasedOnType(Pin);
}
// End UK2Node Interface
////////////////////////////////////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Begin UK2Node_Switch Interface
FEdGraphPinType UDialogueK2Node_SwitchDialogueCallback::GetPinType() const
{
	FEdGraphPinType PinType;
	PinType.PinCategory = UEdGraphSchema_K2::PC_Name;
	return PinType;
}

void UDialogueK2Node_SwitchDialogueCallback::RemovePinFromSwitchNode(UEdGraphPin* TargetPin)
{
	// Stop removing option pins, should never arrive here
	if (TargetPin != GetDefaultPin())
	{
		return;
	}

	Super::RemovePinFromSwitchNode(TargetPin);
}

bool UDialogueK2Node_SwitchDialogueCallback::CanRemoveExecutionPin(UEdGraphPin* TargetPin) const
{
	// Do not allow to remove normal pins, only the execution pin
	if (TargetPin != GetDefaultPin())
	{
		return false;
	}

	return Super::CanRemoveExecutionPin(TargetPin);
}

FName UDialogueK2Node_SwitchDialogueCallback::GetPinNameGivenIndex(int32 Index) const
{
	return Index < DialoguePinNames.Num() ? DialoguePinNames[Index] : NAME_None;
}

FName UDialogueK2Node_SwitchDialogueCallback::GetUniquePinName()
{
	checkNoEntry();
	return NAME_None;
}

void UDialogueK2Node_SwitchDialogueCallback::CreateCasePins()
{
	const UEdGraphSchema_K2* K2Schema = GetDefault<UEdGraphSchema_K2>();
	for (const auto& PinName : DialoguePinNames)
	{
		CreatePin(EGPD_Output, K2Schema->PC_Exec, PinName);
	}
}

void UDialogueK2Node_SwitchDialogueCallback::CreateFunctionPin()
{
	// Copied from base class, because it is not exported
	// TODO PR
	// Set properties on the function pin
	UEdGraphPin* FunctionPin = CreatePin(EGPD_Input, UEdGraphSchema_K2::PC_Object, FunctionClass, FunctionName);
	FunctionPin->bDefaultValueIsReadOnly = true;
	FunctionPin->bNotConnectable = true;
	FunctionPin->bHidden = true;

#if ENGINE_MINOR_VERSION >= 25
	UFunction* Function = FindUField<UFunction>(FunctionClass, FunctionName);
#else
	UFunction* Function = FindField<UFunction>(FunctionClass, FunctionName);
#endif
	const bool bIsStaticFunc = Function->HasAllFunctionFlags(FUNC_Static);
	if (bIsStaticFunc)
	{
		// Wire up the self to the CDO of the class if it's not us
		if (UBlueprint* BP = GetBlueprint())
		{
			UClass* FunctionOwnerClass = Function->GetOuterUClass();
			if (!BP->SkeletonGeneratedClass->IsChildOf(FunctionOwnerClass))
			{
				FunctionPin->DefaultObject = FunctionOwnerClass->GetDefaultObject();
			}
		}
	}
}

void UDialogueK2Node_SwitchDialogueCallback::AddPinToSwitchNode()
{
	// overwrite default behaviour, should never be used
	if (RefreshPinNames())
	{
		ReconstructNode();
	}
}
// End UK2Node_Switch Interface
////////////////////////////////////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Begin own functions
bool UDialogueK2Node_SwitchDialogueCallback::RefreshPinNames()
{
	static constexpr bool bBlueprintMustBeLoaded = true;
	const FName ParticipantName = FDialogueBlueprintUtilities::GetParticipantNameFromNode(this, bBlueprintMustBeLoaded);
	if (ParticipantName == NAME_None)
	{
		return false;
	}

	TArray<FName> NewPinNames;
	switch (CallbackType)
	{
		case EDlgDialogueCallback::Event:
			NewPinNames.Append(UDlgManager::GetDialoguesParticipantEventNames(ParticipantName));
			break;

		case EDlgDialogueCallback::Condition:
			NewPinNames.Append(UDlgManager::GetDialoguesParticipantConditionNames(ParticipantName));
			break;

		case EDlgDialogueCallback::FloatValue:
			NewPinNames.Append(UDlgManager::GetDialoguesParticipantFloatNames(ParticipantName));
			break;

		case EDlgDialogueCallback::IntValue:
			NewPinNames.Append(UDlgManager::GetDialoguesParticipantIntNames(ParticipantName));
			break;

		case EDlgDialogueCallback::BoolValue:
			NewPinNames.Append(UDlgManager::GetDialoguesParticipantBoolNames(ParticipantName));
			break;

		case EDlgDialogueCallback::NameValue:
			NewPinNames.Append(UDlgManager::GetDialoguesParticipantFNameNames(ParticipantName));
			break;

		default:
			unimplemented();
	}

	// Size changed, simply copy
	if (NewPinNames.Num() != DialoguePinNames.Num())
	{
		DialoguePinNames = NewPinNames;
		return true;
	}

	// Find any difference, if any
	for (int32 i = 0; i < NewPinNames.Num(); ++i)
	{
		if (NewPinNames[i] != DialoguePinNames[i])
		{
			DialoguePinNames = NewPinNames;
			return true;
		}
	}

	return false;
}
// End own functions
////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#undef LOCTEXT_NAMESPACE
