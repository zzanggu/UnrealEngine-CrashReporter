﻿// Copyright 1998-2015 Epic Games, Inc. All Rights Reserved.

#include "BlueprintGraphPrivatePCH.h"
#include "KismetCompiler.h"
#include "MakeStructHandler.h"
#include "BlueprintFieldNodeSpawner.h"
#include "EditorCategoryUtils.h"
#include "BlueprintActionDatabaseRegistrar.h"
#include "BlueprintActionFilter.h"	// for FBlueprintActionContext

#define LOCTEXT_NAMESPACE "K2Node_MakeStruct"

//////////////////////////////////////////////////////////////////////////
// UK2Node_MakeStruct


UK2Node_MakeStruct::FMakeStructPinManager::FMakeStructPinManager(const uint8* InSampleStructMemory)
	: FStructOperationOptionalPinManager(), SampleStructMemory(InSampleStructMemory)
{
}

void UK2Node_MakeStruct::FMakeStructPinManager::CustomizePinData(UEdGraphPin* Pin, FName SourcePropertyName, int32 ArrayIndex, UProperty* Property) const
{
	struct FPinDefaultValueHelper
	{
		static void Set(UEdGraphPin& InPin, const FString& Value, bool bIsText)
		{
			InPin.AutogeneratedDefaultValue = Value;
			if (bIsText)
			{
				InPin.DefaultTextValue = FText::FromString(Value);
			}
			else
			{
				InPin.DefaultValue = Value;
			}
		}
	};

	UK2Node_StructOperation::FStructOperationOptionalPinManager::CustomizePinData(Pin, SourcePropertyName, ArrayIndex, Property);
	if (Pin && Property)
	{
		const UEdGraphSchema_K2* Schema = GetDefault<UEdGraphSchema_K2>();
		check(Schema);
		UArrayProperty* const ArrayProperty = Cast<UArrayProperty>(Property);
		const bool bIsText = Property->IsA<UTextProperty>() || (ArrayProperty && ArrayProperty->Inner && ArrayProperty->Inner->IsA<UTextProperty>());
		checkSlow(bIsText == (Schema->PC_Text == Pin->PinType.PinCategory));

		const FString& MetadataDefaultValue = Property->GetMetaData(TEXT("MakeStructureDefaultValue"));
		if (!MetadataDefaultValue.IsEmpty())
		{
			FPinDefaultValueHelper::Set(*Pin, MetadataDefaultValue, bIsText);
			return;
		}

		if (NULL != SampleStructMemory)
		{
			FString NewDefaultValue;
			if (Property->ExportText_InContainer(0, NewDefaultValue, SampleStructMemory, SampleStructMemory, NULL, PPF_None))
			{
				if (Schema->IsPinDefaultValid(Pin, NewDefaultValue, NULL, FText::GetEmpty()).IsEmpty())
				{
					FPinDefaultValueHelper::Set(*Pin, NewDefaultValue, bIsText);
					return;
				}
			}
		}

		Schema->SetPinDefaultValueBasedOnType(Pin);
	}
}

bool UK2Node_MakeStruct::FMakeStructPinManager::CanTreatPropertyAsOptional(UProperty* TestProperty) const
	{
		return UK2Node_MakeStruct::CanBeExposed(TestProperty);
	}

UK2Node_MakeStruct::UK2Node_MakeStruct(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

void UK2Node_MakeStruct::AllocateDefaultPins()
{
	const UEdGraphSchema_K2* Schema = GetDefault<UEdGraphSchema_K2>();
	if(Schema && StructType)
	{
		CreatePin(EGPD_Output, Schema->PC_Struct, TEXT(""), StructType, false, false, StructType->GetName());
		
		{
			FStructOnScope StructOnScope(Cast<UScriptStruct>(StructType));
			FMakeStructPinManager OptionalPinManager(StructOnScope.GetStructMemory());
			OptionalPinManager.RebuildPropertyList(ShowPinForProperties, StructType);
			OptionalPinManager.CreateVisiblePins(ShowPinForProperties, StructType, EGPD_Input, this);
		}

		// When struct has a lot of fields, mark their pins as advanced
		if(Pins.Num() > 5)
		{
			if(ENodeAdvancedPins::NoPins == AdvancedPinDisplay)
			{
				AdvancedPinDisplay = ENodeAdvancedPins::Hidden;
			}

			for(int32 PinIndex = 3; PinIndex < Pins.Num(); ++PinIndex)
			{
				if(UEdGraphPin * EdGraphPin = Pins[PinIndex])
				{
					EdGraphPin->bAdvancedView = true;
				}
			}
		}
	}
}

void UK2Node_MakeStruct::ValidateNodeDuringCompilation(class FCompilerResultsLog& MessageLog) const
{
	if(!StructType)
	{
		MessageLog.Error(*LOCTEXT("NoStruct_Error", "No Struct in @@").ToString(), this);
	}
	else
	{
		bool bHasAnyBlueprintVisibleProperty = false;
		for (TFieldIterator<UProperty> It(StructType); It; ++It)
		{
			const UProperty* Property = *It;
			if (CanBeExposed(Property))
			{
				const bool bIsBlueprintVisible = Property->HasAnyPropertyFlags(CPF_BlueprintVisible) || (Property->GetOwnerStruct() && Property->GetOwnerStruct()->IsA<UUserDefinedStruct>());
				bHasAnyBlueprintVisibleProperty |= bIsBlueprintVisible;

				const UEdGraphPin* Pin = Property ? FindPin(Property->GetName()) : NULL;

				if (Pin && !bIsBlueprintVisible)
				{
					MessageLog.Warning(*LOCTEXT("PropertyIsNotBPVisible_Warning", "@@ - the native property is not tagged as BlueprintReadWrite, the pin will be removed in a future release.").ToString(), Pin);
				}

				if (Property->ArrayDim > 1)
				{
					MessageLog.Warning(*LOCTEXT("StaticArray_Warning", "@@ - the native property is a static array, which is not supported by blueprints").ToString(), Pin);
				}
			}
		}

		if (!bHasAnyBlueprintVisibleProperty)
		{
			MessageLog.Warning(*LOCTEXT("StructHasNoBPVisibleProperties_Warning", "@@ has no property tagged as BlueprintReadWrite. The node will be removed in a future release.").ToString(), this);
		}
	}
}


FText UK2Node_MakeStruct::GetNodeTitle(ENodeTitleType::Type TitleType) const 
{
	if (StructType == nullptr)
	{
		return LOCTEXT("MakeNullStructTitle", "Make <unknown struct>");
	}
	else if (CachedNodeTitle.IsOutOfDate())
	{
		FFormatNamedArguments Args;
		Args.Add(TEXT("StructName"), FText::FromName(StructType->GetFName()));
		// FText::Format() is slow, so we cache this to save on performance
		CachedNodeTitle = FText::Format(LOCTEXT("MakeNodeTitle", "Make {StructName}"), Args);
	}
	return CachedNodeTitle;
}

FText UK2Node_MakeStruct::GetTooltipText() const
{
	if (StructType == nullptr)
	{
		return LOCTEXT("MakeNullStruct_Tooltip", "Adds a node that create an '<unknown struct>' from its members");
	}
	else if (CachedTooltip.IsOutOfDate())
	{
		// FText::Format() is slow, so we cache this to save on performance
		CachedTooltip = FText::Format(
			LOCTEXT("MakeStruct_Tooltip", "Adds a node that create a '{0}' from its members"),
			FText::FromName(StructType->GetFName())
		);
	}
	return CachedTooltip;
}

FLinearColor UK2Node_MakeStruct::GetNodeTitleColor() const
{
	if(const UEdGraphSchema_K2* K2Schema = GetDefault<UEdGraphSchema_K2>())
	{
		FEdGraphPinType PinType;
		PinType.PinCategory = K2Schema->PC_Struct;
		PinType.PinSubCategoryObject = StructType;
		return K2Schema->GetPinTypeColor(PinType);
	}
	return UK2Node::GetNodeTitleColor();
}

bool UK2Node_MakeStruct::CanBeExposed(const UProperty* Property)
{
	if(Property)
	{
		const UEdGraphSchema_K2* Schema = GetDefault<UEdGraphSchema_K2>();
		check(Schema);

		FEdGraphPinType DumbGraphPinType;
		const bool bConvertable = Schema->ConvertPropertyToPinType(Property, /*out*/ DumbGraphPinType);

		//TODO: remove CPF_Edit in a future release
		const bool bVisible = Property->HasAnyPropertyFlags(CPF_BlueprintVisible|CPF_Edit) && !(Property->ArrayDim > 1);
		const bool bBlueprintReadOnly = Property->HasAllPropertyFlags(CPF_BlueprintReadOnly);
		if(bVisible && bConvertable && !bBlueprintReadOnly)
		{
			return true;
		}
	}
	return false;
}

bool UK2Node_MakeStruct::CanBeMade(const UScriptStruct* Struct)
{
	if(!Struct->HasMetaData(TEXT("HasNativeMake")))
	{
		for (TFieldIterator<UProperty> It(Struct); It; ++It)
		{
			if(CanBeExposed(*It))
			{
				return true;
			}
		}
	}
	return false;
}

FNodeHandlingFunctor* UK2Node_MakeStruct::CreateNodeHandler(FKismetCompilerContext& CompilerContext) const
{
	return new FKCHandler_MakeStruct(CompilerContext);
}

UK2Node::ERedirectType UK2Node_MakeStruct::DoPinsMatchForReconstruction(const UEdGraphPin* NewPin, int32 NewPinIndex, const UEdGraphPin* OldPin, int32 OldPinIndex)  const
{
	ERedirectType Result = UK2Node::DoPinsMatchForReconstruction(NewPin, NewPinIndex, OldPin, OldPinIndex);
	if ((ERedirectType_None == Result) && DoRenamedPinsMatch(NewPin, OldPin, false))
	{
		Result = ERedirectType_Custom;
	}
	else if ((ERedirectType_None == Result) && NewPin && OldPin)
	{
		const UEdGraphSchema_K2* K2Schema = GetDefault<UEdGraphSchema_K2>();
		if ((EGPD_Output == NewPin->Direction) && (EGPD_Output == OldPin->Direction))
		{
			const UEdGraphSchema_K2* K2Schema = GetDefault<UEdGraphSchema_K2>();
			if (K2Schema->ArePinTypesCompatible(NewPin->PinType, OldPin->PinType))
			{
				Result = ERedirectType_Custom;
			}
		}
		else if ((EGPD_Input == NewPin->Direction) && (EGPD_Input == OldPin->Direction))
		{
			TMap<FName, FName>* StructRedirects = UStruct::TaggedPropertyRedirects.Find(StructType->GetFName());
			if (StructRedirects)
			{
				FName* PropertyRedirect = StructRedirects->Find(FName(*OldPin->PinName));
				if (PropertyRedirect)
				{
					Result = ((FCString::Stricmp(*PropertyRedirect->ToString(), *NewPin->PinName) != 0) ? ERedirectType_None : ERedirectType_Name);
				}
			}
		}
	}
	return Result;
}

void UK2Node_MakeStruct::GetMenuActions(FBlueprintActionDatabaseRegistrar& ActionRegistrar) const
{
	auto SetNodeStructLambda = [](UEdGraphNode* NewNode, UField const* /*StructField*/, TWeakObjectPtr<UScriptStruct> NonConstStructPtr)
	{
		UK2Node_MakeStruct* MakeNode = CastChecked<UK2Node_MakeStruct>(NewNode);
		MakeNode->StructType = NonConstStructPtr.Get();
	};

	auto CategoryOverrideLambda = [](FBlueprintActionContext const& Context, IBlueprintNodeBinder::FBindingSet const& /*Bindings*/, FBlueprintActionUiSpec* UiSpecOut, TWeakObjectPtr<UScriptStruct> StructPtr)
	{
		for (UEdGraphPin* Pin : Context.Pins)
		{
			UScriptStruct* PinStruct = Cast<UScriptStruct>(Pin->PinType.PinSubCategoryObject.Get());
			if ((PinStruct != nullptr) && (StructPtr.Get() == PinStruct) && (Pin->Direction == EGPD_Input))
			{
				UiSpecOut->Category = LOCTEXT("EmptyCategory", "|");
				break;
			}
		}
	};

	for (TObjectIterator<UScriptStruct> StructIt; StructIt; ++StructIt)
	{
		UScriptStruct const* Struct = (*StructIt);
		if (!UEdGraphSchema_K2::IsAllowableBlueprintVariableType(Struct) || !CanBeMade(Struct))
		{
			continue;
		}

		// to keep from needlessly instantiating a UBlueprintNodeSpawners, first   
		// check to make sure that the registrar is looking for actions of this type
		// (could be regenerating actions for a specific asset, and therefore the 
		// registrar would only accept actions corresponding to that asset)
		if (!ActionRegistrar.IsOpenForRegistration(Struct))
		{
			continue;
		}

		UBlueprintFieldNodeSpawner* NodeSpawner = UBlueprintFieldNodeSpawner::Create(GetClass(), Struct);
		check(NodeSpawner != nullptr);
		TWeakObjectPtr<UScriptStruct> NonConstStructPtr = Struct;
		NodeSpawner->SetNodeFieldDelegate = UBlueprintFieldNodeSpawner::FSetNodeFieldDelegate::CreateStatic(SetNodeStructLambda, NonConstStructPtr);
		NodeSpawner->DynamicUiSignatureGetter = UBlueprintFieldNodeSpawner::FUiSpecOverrideDelegate::CreateStatic(CategoryOverrideLambda, NonConstStructPtr);

		// this struct could belong to a class, or is a user defined struct 
		// (asset), that's why we want to make sure to register it along with 
		// the action (so the action knows to refresh when the class/asset is).
		ActionRegistrar.AddBlueprintAction(Struct, NodeSpawner);
	}
}

FText UK2Node_MakeStruct::GetMenuCategory() const
{
	return FEditorCategoryUtils::GetCommonCategory(FCommonEditorCategory::Struct);
}

#undef LOCTEXT_NAMESPACE
