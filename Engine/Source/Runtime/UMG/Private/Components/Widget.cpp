// Copyright 1998-2014 Epic Games, Inc. All Rights Reserved.

#include "UMGPrivatePCH.h"

/////////////////////////////////////////////////////
// UWidget

UWidget::UWidget(const FPostConstructInitializeProperties& PCIP)
	: Super(PCIP)
{
	bIsEnabled = true;
	bIsVariable = true;
	bDesignTime = false;
	Visiblity = ESlateVisibility::Visible;
	RenderTransformPivot = FVector2D(0.5f, 0.5f);
}

void UWidget::SetRenderTransform(FWidgetTransform Transform)
{
	RenderTransform = Transform;
	UpdateRenderTransform();
}

void UWidget::SetRenderScale(FVector2D Scale)
{
	RenderTransform.Scale = Scale;
	UpdateRenderTransform();
}

void UWidget::SetRenderShear(FVector2D Shear)
{
	RenderTransform.Shear = Shear;
	UpdateRenderTransform();
}

void UWidget::SetRenderAngle(float Angle)
{
	RenderTransform.Angle = Angle;
	UpdateRenderTransform();
}

void UWidget::SetRenderTranslation(FVector2D Translation)
{
	RenderTransform.Translation = Translation;
	UpdateRenderTransform();
}

void UWidget::UpdateRenderTransform()
{
	if ( MyWidget.IsValid() )
	{
		if (!RenderTransform.IsIdentity())
		{
			FSlateRenderTransform Transform2D = ::Concatenate(FScale2D(RenderTransform.Scale), FShear2D::FromShearAngles(RenderTransform.Shear), FQuat2D(FMath::DegreesToRadians(RenderTransform.Angle)), FVector2D(RenderTransform.Translation));
			MyWidget.Pin()->SetRenderTransform(Transform2D);
		}
		else
		{
			MyWidget.Pin()->SetRenderTransform(TOptional<FSlateRenderTransform>());
		}
	}
}

void UWidget::SetRenderTransformPivot(FVector2D Pivot)
{
	RenderTransformPivot = Pivot;
	if ( MyWidget.IsValid() )
	{
		MyWidget.Pin()->SetRenderTransformPivot(Pivot);
	}
}

bool UWidget::GetIsEnabled() const
{
	return MyWidget.IsValid() ? MyWidget.Pin()->IsEnabled() : bIsEnabled;
}

void UWidget::SetIsEnabled(bool bInIsEnabled)
{
	bIsEnabled = bInIsEnabled;
	if ( MyWidget.IsValid() )
	{
		MyWidget.Pin()->SetEnabled(bInIsEnabled);
	}
}

TEnumAsByte<ESlateVisibility::Type> UWidget::GetVisibility()
{
	if ( MyWidget.IsValid() )
	{
		return UWidget::ConvertRuntimeToSerializedVisiblity(MyWidget.Pin()->GetVisibility());
	}

	return Visiblity;
}

void UWidget::SetVisibility(TEnumAsByte<ESlateVisibility::Type> InVisibility)
{
	Visiblity = InVisibility;

	if ( MyWidget.IsValid() )
	{
		return MyWidget.Pin()->SetVisibility(UWidget::ConvertSerializedVisibilityToRuntime(InVisibility));
	}
}

void UWidget::SetToolTipText(const FText& InToolTipText)
{
	ToolTipText = InToolTipText;

	if ( MyWidget.IsValid() )
	{
		return MyWidget.Pin()->SetToolTipText(InToolTipText);
	}
}

bool UWidget::IsHovered() const
{
	if ( MyWidget.IsValid() )
	{
		return MyWidget.Pin()->IsHovered();
	}

	return false;
}

bool UWidget::HasKeyboardFocus() const
{
	if ( MyWidget.IsValid() )
	{
		return MyWidget.Pin()->HasKeyboardFocus();
	}

	return false;
}

bool UWidget::HasFocusedDescendants() const
{
	if ( MyWidget.IsValid() )
	{
		return MyWidget.Pin()->HasFocusedDescendants();
	}

	return false;
}

bool UWidget::HasMouseCapture() const
{
	if ( MyWidget.IsValid() )
	{
		return MyWidget.Pin()->HasMouseCapture();
	}

	return false;
}

void UWidget::SetKeyboardFocus() const
{
	if ( MyWidget.IsValid() )
	{
		FSlateApplication::Get().SetKeyboardFocus(MyWidget.Pin());
	}
}

void UWidget::ForceLayoutPrepass()
{
	if ( MyWidget.IsValid() )
	{
		MyWidget.Pin()->SlatePrepass();
	}
}

FVector2D UWidget::GetDesiredSize() const
{
	if ( MyWidget.IsValid() )
	{
		return MyWidget.Pin()->GetDesiredSize();
	}

	return FVector2D(0, 0);
}

UPanelWidget* UWidget::GetParent() const
{
	if ( Slot )
	{
		return Slot->Parent;
	}

	return nullptr;
}

void UWidget::RemoveFromParent()
{
	UPanelWidget* CurrentParent = GetParent();
	if ( CurrentParent )
	{
		CurrentParent->RemoveChild(this);
	}
}

TSharedRef<SWidget> UWidget::TakeWidget()
{
	TSharedPtr<SWidget> SafeWidget;
	bool bNewlyCreated = false;

	// If the underlying widget doesn't exist we need to construct and cache the widget for the first run.
	if ( !MyWidget.IsValid() )
	{
		SafeWidget = RebuildWidget();
		MyWidget = SafeWidget;

		bNewlyCreated = true;
	}
	else
	{
		SafeWidget = MyWidget.Pin();
	}

	// If it is a user widget wrap it in a SObjectWidget to keep the instance from being GC'ed
	if ( IsA(UUserWidget::StaticClass()) )
	{
		// If the GC Widget is still valid we still exist in the slate hierarchy, so just return the GC Widget.
		if ( MyGCWidget.IsValid() )
		{
			return MyGCWidget.Pin().ToSharedRef();
		}
		// Otherwise we need to recreate the wrapper widget
		else
		{
			TSharedPtr<SWidget> SafeGCWidget = SNew(SObjectWidget, Cast<UUserWidget>(this))
				[
					SafeWidget.ToSharedRef()
				];

			MyGCWidget = SafeGCWidget;

			SynchronizeProperties();

			return SafeGCWidget.ToSharedRef();
		}
	}
	else
	{
		if ( bNewlyCreated )
		{
			SynchronizeProperties();
		}

		return SafeWidget.ToSharedRef();
	}
}

TSharedPtr<SWidget> UWidget::GetCachedWidget() const
{
	if ( MyGCWidget.IsValid() )
	{
		return MyGCWidget.Pin();
	}

	return MyWidget.Pin();
}

TSharedRef<SWidget> UWidget::BuildDesignTimeWidget(TSharedRef<SWidget> WrapWidget)
{
	if (IsDesignTime())
	{
		return SNew(SOverlay)
		
		+ SOverlay::Slot()
		.HAlign(HAlign_Fill)
		.VAlign(VAlign_Fill)
		[
			WrapWidget
		]
		
		+ SOverlay::Slot()
		.HAlign(HAlign_Fill)
		.VAlign(VAlign_Fill)
		[
			SNew(SBorder)
			.Visibility( EVisibility::HitTestInvisible )
			.BorderImage(FUMGStyle::Get().GetBrush("MarchingAnts"))
		];
	}
	else
	{
		return WrapWidget;
	}
}

#if WITH_EDITOR
#define LOCTEXT_NAMESPACE "UMGEditor"

bool UWidget::IsGeneratedName() const
{
	FString Name = GetName();
	FString BaseName = GetClass()->GetName() + "_";

	if ( Name.StartsWith(BaseName) )
	{
		return true;
	}

	return false;
}

FString UWidget::GetLabelMetadata() const
{
	return "";
}

FString UWidget::GetLabel() const
{
	if ( IsGeneratedName() && !bIsVariable )
	{
		return "[" + GetClass()->GetName() + "]" + GetLabelMetadata();
	}
	else
	{
		return GetName();
	}
}

const FText UWidget::GetPaletteCategory()
{
	return LOCTEXT("Uncategorized", "Uncategorized");
}

const FSlateBrush* UWidget::GetEditorIcon()
{
	return FUMGStyle::Get().GetBrush("Widget");
}

EVisibility UWidget::GetVisibilityInDesigner() const
{
	return bHiddenInDesigner ? EVisibility::Collapsed : EVisibility::Visible;
}

void UWidget::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	if ( MyWidget.IsValid() )
	{
		SynchronizeProperties();
	}
}

void UWidget::Select()
{
	OnSelected();

	UWidget* Parent = GetParent();
	while ( Parent != nullptr )
	{
		Parent->OnDescendantSelected(this);
		Parent = Parent->GetParent();
	}
}

void UWidget::Deselect()
{
	OnDeselected();

	UWidget* Parent = GetParent();
	while ( Parent != nullptr )
	{
		Parent->OnDescendantDeselected(this);
		Parent = Parent->GetParent();
	}
}

#undef LOCTEXT_NAMESPACE
#endif

bool UWidget::Modify(bool bAlwaysMarkDirty)
{
	bool Modified = Super::Modify(bAlwaysMarkDirty);

	if ( Slot )
	{
		Modified &= Slot->Modify(bAlwaysMarkDirty);
	}

	return Modified;
}

bool UWidget::IsChildOf(UWidget* PossibleParent)
{
	UPanelWidget* Parent = GetParent();
	if ( Parent == nullptr )
	{
		return false;
	}
	else if ( Parent == PossibleParent )
	{
		return true;
	}
	
	return Parent->IsChildOf(PossibleParent);
}

TSharedRef<SWidget> UWidget::RebuildWidget()
{
	ensureMsg(false, TEXT("You must implement RebuildWidget() in your child class"));
	return SNew(SSpacer);
}

void UWidget::SynchronizeProperties()
{
	// We want to apply the bindings to the cached widget, which could be the SWidget, or the SObjectWidget, 
	// in the case where it's a user widget.  We always want to prefer the SObjectWidget so that bindings to 
	// visibility and enabled status are not stomping values setup in the root widget in the User Widget.
	TSharedPtr<SWidget> SafeWidget = GetCachedWidget();

#if WITH_EDITOR
	// Always use an enabled and visible state in the designer.
	if ( IsDesignTime() )
	{
		SafeWidget->SetEnabled(true);
		SafeWidget->SetVisibility(TAttribute<EVisibility>::Create(TAttribute<EVisibility>::FGetter::CreateUObject(this, &UWidget::GetVisibilityInDesigner)));
	}
	else
#endif
	{
		SafeWidget->SetEnabled(OPTIONAL_BINDING(bool, bIsEnabled));
		SafeWidget->SetVisibility(OPTIONAL_BINDING_CONVERT(ESlateVisibility::Type, Visiblity, EVisibility, ConvertVisibility));
	}

	UpdateRenderTransform();
	SafeWidget->SetRenderTransformPivot(RenderTransformPivot);

	if ( !ToolTipText.IsEmpty() )
	{
		SafeWidget->SetToolTipText(OPTIONAL_BINDING(FText, ToolTipText));
	}
}

bool UWidget::IsDesignTime() const
{
	return bDesignTime;
}

void UWidget::SetIsDesignTime(bool bInDesignTime)
{
	bDesignTime = bInDesignTime;
}

EVisibility UWidget::ConvertSerializedVisibilityToRuntime(ESlateVisibility::Type Input)
{
	switch ( Input )
	{
	case ESlateVisibility::Visible:
		return EVisibility::Visible;
	case ESlateVisibility::Collapsed:
		return EVisibility::Collapsed;
	case ESlateVisibility::Hidden:
		return EVisibility::Hidden;
	case ESlateVisibility::HitTestInvisible:
		return EVisibility::HitTestInvisible;
	case ESlateVisibility::SelfHitTestInvisible:
		return EVisibility::SelfHitTestInvisible;
	default:
		//check(false);
		return EVisibility::Visible;
	}
}

ESlateVisibility::Type UWidget::ConvertRuntimeToSerializedVisiblity(const EVisibility& Input)
{
	if ( Input == EVisibility::Visible )
	{
		return ESlateVisibility::Visible;
	}
	else if ( Input == EVisibility::Collapsed )
	{
		return ESlateVisibility::Collapsed;
	}
	else if ( Input == EVisibility::Hidden )
	{
		return ESlateVisibility::Hidden;
	}
	else if ( Input == EVisibility::HitTestInvisible )
	{
		return ESlateVisibility::HitTestInvisible;
	}
	else if ( Input == EVisibility::SelfHitTestInvisible )
	{
		return ESlateVisibility::SelfHitTestInvisible;
	}
	else
	{
		//check(false);
		return ESlateVisibility::Visible;
	}
}

FSizeParam UWidget::ConvertSerializedSizeParamToRuntime(const FSlateChildSize& Input)
{
	switch ( Input.SizeRule )
	{
	default:
	case ESlateSizeRule::Automatic:
		return FAuto();
	case ESlateSizeRule::Fill:
		return FStretch(Input.Value);
	}

	return FAuto();
}

void UWidget::GatherChildren(UWidget* Root, TSet<UWidget*>& Children)
{
	UPanelWidget* PanelRoot = Cast<UPanelWidget>(Root);
	if ( PanelRoot )
	{
		for ( int32 ChildIndex = 0; ChildIndex < PanelRoot->GetChildrenCount(); ChildIndex++ )
		{
			UWidget* ChildWidget = PanelRoot->GetChildAt(ChildIndex);
			Children.Add(ChildWidget);
		}
	}
}

void UWidget::GatherAllChildren(UWidget* Root, TSet<UWidget*>& Children)
{
	UPanelWidget* PanelRoot = Cast<UPanelWidget>(Root);
	if ( PanelRoot )
	{
		for ( int32 ChildIndex = 0; ChildIndex < PanelRoot->GetChildrenCount(); ChildIndex++ )
		{
			UWidget* ChildWidget = PanelRoot->GetChildAt(ChildIndex);
			Children.Add(ChildWidget);

			GatherAllChildren(ChildWidget, Children);
		}
	}
}

UWidget* UWidget::FindChildContainingDescendant(UWidget* Root, UWidget* Descendant)
{
	UWidget* Parent = Descendant->GetParent();

	while ( Parent != nullptr )
	{
		// If the Descendant's parent is the root, then the child containing the descendant is the descendant.
		if ( Parent == Root )
		{
			return Descendant;
		}

		Descendant = Parent;
		Parent = Parent->GetParent();
	}

	return nullptr;
}
