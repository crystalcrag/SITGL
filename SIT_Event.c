/*
 * SIT_Event.c : dispatch events to user callbacks.
 *
 * Written by T.Pierron, apr 2020.
 */

#include <string.h>
#include <ctype.h>
#include <math.h>
#include "SIT_P.h"
#include "SIT_CSSParser.h"
#include "SIT_CSSLayout.h"

DLLIMP int SIT_ApplyCallback(SIT_Widget w, APTR cd, int type)
{
	static uint32_t globalEvt =
		SET_EVT(SITE_OnClose) | SET_EVT(SITE_OnActivate) | SET_EVT(SITE_OnChange) |
		SET_EVT(SITE_OnFocus) | SET_EVT(SITE_OnBlur);

	if (w == NULL) return -1;

	SIT_Callback cb, next;
	int oldf = w->flags & SITF_IsLocked, ret = 1 << type;

	if ((globalEvt & ret) && (sit.root->evtFlags & ret))
	{
		/* global cb registered: trigger then before action is taken */
		for (cb = HEAD(sit.root->callbacks); cb; NEXT(cb))
		{
			if (cb->sc_Event != type) continue;
			ret = cb->sc_CB(w, cd, cb->sc_UserData);
			if (ret) break;
		}
	}

	for (ret = 0, cb = next = (SIT_Callback) w->callbacks.lh_Head; cb; cb = next)
	{
		next = (APTR) cb->sc_Node.ln_Next;
		if (cb->sc_Event != type) continue;

		/* user might delete this control in the callback */
		w->flags |= SITF_IsLocked;

		if (w->type == SIT_BUTTON && type == SITE_OnActivate)
		{
			SIT_Button button = (SIT_Button) w;
			cd = (APTR) (button->curValue ? *button->curValue : button->state);
		}

		ret = cb->sc_CB(w, cd, cb->sc_UserData);

		if (ret || ! next) break;
	}
	w->flags &= ~SITF_IsLocked;
	w->flags |= oldf;

	if (ret == 0 && type == SITE_OnActivate)
	{
		/* event not processed: set some default action */
		if (w->type == SIT_BUTTON && ((SIT_Button)w)->type == SITV_CancelButton)
		{
			/* forcibly close the dialog */
			SIT_CloseDialog(w);
		}
		else if (w->type == SIT_HTMLTAG && (w->style.flags & CSSF_LINK))
		{
			/* click on <a>, check for href url */
			STRPTR href = SIT_GetHTMLAttr(w, "href");
			if (! IsDef(href) || strcmp(href, "#") == 0)
			{
				TEXT path[256];
				layoutGetTextContent(w, path, sizeof path);
				while (! IsDir(path) && ParentDir(path) >= 0);
				OpenDocument(path);
			}
			else OpenDocument(href);
		}
	}

	return ret;
}

static SIT_Widget SIT_EventBubble(SIT_Widget w, int type)
{
	/* bubble up until we have a control that has an event registered */
	if (w->type == SIT_BUTTON) return w;
	if (w->type == SIT_HTMLTAG && (w->style.flags & CSSF_LINK))
	{
		if (HAS_EVT(w, type)) return w;
		SIT_Widget parent;
		for (parent = w->parent; parent && parent->type == SIT_HTMLTAG; parent = parent->parent);
		return parent && HAS_EVT(parent, type) ? parent : w;
	}
	while (w && (w->enabled == 0 || ! HAS_EVT(w, type)))
		w = w->parent;
	return w;
}

/*
 * process keyboard events
 */
static SIT_Widget SIT_NextFocusRingSub(SIT_Widget start, int dir)
{
	SIT_Widget root = NULL;
	if (! start || (start->flags & SITF_InFocusRing) == 0 || (dir > 0 && start->nextCtrl == NULL) || dir < 0)
	{
		if (start == NULL)
			start = root = sit.activeDlg;
		else
			/* get the first/last focus item in the top level group */
			for (root = start; (root->flags & SITF_TopLevel) == 0; root = root->parent);
		if (dir > 0)
			return FOCUSRING(root);
	}
	else return start->nextCtrl;

	root = FOCUSRING(root);
	if (root == start) start = NULL;
	while (root && root->nextCtrl && root->nextCtrl != start)
		root = root->nextCtrl;
	return root;
}

/* need to take care of disabled controls :-/ */
static SIT_Widget SIT_NextFocusRing(SIT_Widget start, int dir)
{
	SIT_Widget next = SIT_NextFocusRingSub(start, dir);

	while (next && next != start && next->enabled == 0)
		next = SIT_NextFocusRingSub(next, dir);

	return next;
}

#define SITK_FlagPhony    (SITK_FlagCapture | SITK_FlagUp)

/* check for accelerator table */
static Bool SIT_ProcessAccel(int capture, int key)
{
	SIT_Widget  parent;
	SIT_Accel * a;
	int         k = key & 0xff;

	parent = sit.activeDlg;
	if (parent == NULL) return False;
	if ((parent->flags & SITF_HasAccel) == 0)
	{
		if (parent == sit.root) return False;
		parent = sit.root;
		if ((parent->flags & SITF_HasAccel) == 0) return False;
	}

	if (1 <= k && k <= 26)
		key |= (key & ~0xff) | SITK_FlagCtrl | ('a' + (k - 1));
	else if ('A' <= k && k <= 'Z')
		key += 32;

	for (a = ((SIT_App)parent)->accel; (k = a->key); a ++)
	{
		if ('A' <= (k&0xff) && (k&0xff) <= 'Z') k += 32;
		if (capture == ((k & SITK_FlagCapture) > 0) && key == (k & ~SITK_FlagPhony))
		{
			STRPTR name = a->target;
			int    evt  = a->event;

			SIT_Widget w = IsDef(name) ? SIT_GetById(parent, name) : parent;

			if (w)
			{
				int * exit;
				switch (evt) {
				case SITE_OnClose:
					switch (w->type) {
					case SIT_APP:
						exit = ((SIT_App)w)->exitCode;
						if (exit) *exit = 1;
						break;
					case SIT_DIALOG:
						SIT_CloseDialog(w);
						break;
					default:
						SIT_ApplyCallback(w, NULL, evt);
					}
					break;
				case SITE_OnActivate:
					if (w->type == SIT_BUTTON)
					{
						SIT_ApplyCallback(w, NULL, SITE_OnActivate);
						break;
					}
				default:
					if (! SIT_ApplyCallback(w, NULL, evt) && a->cb && a->cb(w, (APTR) evt, (APTR) (int) a->tag) == 0)
						return False;
				}
			}
			else if (a->cb)
			{
				a->cb(w, (APTR) evt, (APTR)  (int) a->tag);
			}
			return True;
		}
	}
	return False;
}

/* key without unicode */
DLLIMP int SIT_ProcessKey(int key, int modifier, int pressed)
{
	if (pressed)
	{
		/* is it a char instead? */
		if (key < SITK_Home)
		{
			switch (key) {
			case '\r':
			case '\n': key = SITK_Return; break;
			case '\b': key = SITK_BackSpace; break;
			default:
				if (modifier == SITK_FlagCtrl && 'a' <= key && key <= 'z' && key != 'i')
					return SIT_ProcessChar(key - 'a' + 1, 0);
			}
			return False;
		}
		switch (key) {
		case SITK_LShift:
		case SITK_RShift:
		case SITK_Shift:  modifier |= SITK_FlagShift; break;
		case SITK_LCtrl:
		case SITK_RCtrl:  modifier |= SITK_FlagCtrl; break;
		case SITK_LCommand:
		case SITK_RCommand: modifier |= SITK_FlagCmd; break;
		case SITK_LAlt:
		case SITK_RAlt:   modifier |= SITK_FlagAlt; break;
		case SITK_Tab:    return SIT_ProcessChar('\t', modifier);
		}
	}

	sit.keyQual = modifier;
	/* capture keyboard event */
	if (SIT_ProcessAccel(True, (key|modifier)|(pressed?0:SITK_FlagUp)))
		return 1;

	if (pressed)
	{
		SIT_Widget focus = sit.focus;
		#ifdef DEBUG_MALLOC
		if (key == SITK_F1)
			SIT_DebugMalloc(1);
		else
		#endif
		#ifdef DEBUG_SIT
		if (key == SITK_F2)
			fprintf(stderr, "===============================================\n");
		#endif
		if (focus && focus->enabled)
		{
			if (focus->type == SIT_EDITBOX)
			{
				/* EDITBOX will process all keys */
				if (SIT_TextEditKey((SIT_EditBox) sit.focus, key | modifier))
					return 1;
				if (HAS_EVT(focus, SITE_OnRawKey))
				{
					SIT_OnKey msg = {.keycode = key, .flags = modifier};
					if (SIT_ApplyCallback(focus, &msg, SITE_OnRawKey))
						return 1;
				}
			}
			if ((key == SITK_Space || key == SITK_Return) && focus->type == SIT_BUTTON && (focus->state & STATE_KBDFOCUS))
			{
				focus->oldState = focus->state;
				focus->state   |= STATE_ACTIVE|STATE_HOVER;
				sit.active = focus;
				layoutUpdateStyles(focus);
				if (focus->flags & SITF_ImmediateActive)
					SIT_ApplyCallback(focus, NULL, SITE_OnActivate);
				return 1;
			}
			else
			{
				if (key == SITK_Return)
				{
					/* check if there is a default button in range */
					SIT_Widget root = sit.activeDlg;
					SIT_Widget def  = NULL;
					if (root->type == SIT_APP || (def = ((SIT_Dialog)root)->defButton) == NULL)
					{
						for (root = sit.root; root; NEXT(root))
						{
							if (root->type != SIT_DIALOG) continue;
							SIT_Widget act = ((SIT_Dialog)root)->defButton;
							if (act == NULL) continue;
							if (def) { def = NULL; break; }
							else def = act;
						}
					}
					if (def)
					{
						SIT_ApplyCallback(def, NULL, SITE_OnActivate);
						return 1;
					}
				}
				SIT_Widget target = SIT_EventBubble(focus, SITE_OnRawKey);
				if (target && HAS_EVT(target, SITE_OnRawKey))
				{
					SIT_OnKey msg = {.keycode = key, .flags = modifier};
					if (SIT_ApplyCallback(target, &msg, SITE_OnRawKey))
						return 1;
				}
			}
		}
		/* still not processed: look for default control */
		if (sit.activeDlg && sit.activeDlg->type == SIT_DIALOG)
		{
			SIT_Dialog diag = (SIT_Dialog) sit.activeDlg;

			if (key == SITK_Escape && diag->cancelButton)
			{
				SIT_ApplyCallback(diag->cancelButton, NULL, SITE_OnActivate);
			}
			else if (key == SITK_Return && diag->defButton)
			{
				SIT_ApplyCallback(diag->defButton, NULL, SITE_OnActivate);
			}
		}
	}
	else if ((key == SITK_Space || key == SITK_Return) && sit.active && (sit.active->state & STATE_KBDFOCUS) && sit.active->type == SIT_BUTTON)
	{
		SIT_Widget active = sit.active;
		if (active->flags & SITF_ImmediateActive) return 1;
		if (! (((SIT_Button)active)->type == SITV_ToggleButton && ! ((SIT_Button)active)->state))
		{
			active->oldState = active->state;
			active->state   &= ~(STATE_ACTIVE|STATE_HOVER);
			layoutUpdateStyles(active);
			sit.active = NULL;
		}
		if (SIT_ApplyCallback(active, NULL, SITE_OnActivate))
			return 1;
	}
	/* bubble keyboard event */
	return SIT_ProcessAccel(False, (key|modifier)|(pressed?0:SITK_FlagUp));
}

/* key with unicode */
DLLIMP int SIT_ProcessChar(int cp, int modifier)
{
	sit.keyQual = modifier;
	SIT_Widget focus = sit.focus;

	if (SIT_ProcessAccel(True, cp|modifier))
		return 1;

	if (focus && focus->type == SIT_EDITBOX && focus->enabled)
	{
		TEXT utf8[5];
		utf8[CP2UTF8(utf8, cp)] = 0;
		/* if character is not processed by text edit, let it trickle down the event chain */
		if (SIT_TextEditInsertText((SIT_EditBox) focus, utf8))
			return 1;
	}

	if (cp == '\t')
	{
		/* move keyboard focus */
		SIT_Widget next = SIT_NextFocusRing(focus, modifier & SITK_FlagShift ? -1 : 1);
		if (focus)
		{
			focus->oldState = focus->state;
			focus->state &= ~(STATE_FOCUS | STATE_KBDFOCUS);
			layoutUpdateStyles(focus);
			if (HAS_EVT(focus, SITE_OnBlur) || HAS_EVT(sit.root, SITE_OnBlur))
				SIT_ApplyCallback(focus, NULL, SITE_OnBlur);
		}
		if (next)
		{
			next->oldState = next->state;
			next->state |= STATE_KBDFOCUS;
			layoutUpdateStyles(next);
			if (HAS_EVT(next, SITE_OnFocus) || HAS_EVT(sit.root, SITE_OnFocus))
				SIT_ApplyCallback(next, (APTR) 1, SITE_OnFocus);
		}
		sit.focus = next;
		return 1;
	}
	else if (focus)
	{
		focus = SIT_EventBubble(focus, SITE_OnVanillaKey);
		if (focus)
		{
			SIT_OnKey msg = {.flags = modifier};
			CP2UTF8(msg.utf8, cp);
			if (SIT_ApplyCallback(focus, &msg, SITE_OnVanillaKey))
				return 1;
		}
	}
	return SIT_ProcessAccel(False, cp|modifier);
}

/*
 * process mouse clicks
 */
DLLIMP int SIT_ProcessClick(float x, float y, int button, int pressed)
{
	SIT_Widget sendEvt = NULL;
	SIT_Widget hover   = sit.hover;
	SIT_Widget active  = sit.active;
	int        handled = 0;

	if (button == SITOM_ButtonWheelUp || button == SITOM_ButtonWheelDown)
	{
		/* mousewheel: always sent to hovering control */
		if (! pressed || ! hover) return 0;
		SIT_Widget scroll = hover->vscroll;
		if (scroll && scroll->visible && scroll->enabled)
			hover = scroll;
	}
	else if (pressed && button == SITOM_ButtonLeft)
	{
		/* SITV_Transcient: discard popup if clicked outside */
		if (sit.activeDlg->type == SIT_DIALOG && (((SIT_Dialog)sit.activeDlg)->customStyles & SITV_Transcient) && hover && hover != sit.focus)
		{
			SIT_Widget w;
			for (w = hover; w && w != sit.activeDlg; w = w->parent);
			if (w == NULL)
			{
				SIT_CloseDialog(sit.activeDlg);
			}
		}

		if (hover && hover->enabled)
		{
			SIT_Widget w;
			/* HTML tag within button: redirect click at the button level instead */
			for (w = hover; w->type == SIT_HTMLTAG; w = w->parent);
			if (w->type == SIT_BUTTON)
				hover = w;

			if (sit.curTooltip || sit.toolTip)
			{
				SIT_SetValues(sit.curTooltip, SIT_Visible, False, NULL);
				if (sit.toolTip) SIT_ActionReschedule(sit.toolTip, -1, -1);
				sit.toolTip = NULL;
				sit.curTooltip = NULL;
			}
			hover->oldState = hover->state;
			hover->state &= ~STATE_KBDFOCUS;

			if (hover->nextCtrl != NoFocusRing)
			{
				if (hover->flags & SITF_ImmediateActive)
				{
					hover->state |= STATE_ACTIVATED|STATE_FOCUS;
					sendEvt = SIT_EventBubble(hover, SITE_OnActivate);
				}
				else hover->state |= STATE_ACTIVE|STATE_FOCUS;
				if (sit.focus && sit.focus != hover)
				{
					sit.focus->oldState = sit.focus->state;
					sit.focus->state &= ~(STATE_FOCUS | STATE_KBDFOCUS);
					layoutUpdateStyles(sit.focus);
					if (HAS_EVT(sit.focus, SITE_OnBlur) || HAS_EVT(sit.root, SITE_OnBlur))
						SIT_ApplyCallback(sit.focus, NULL, SITE_OnBlur);
				}
				sit.active = sit.focus = hover;
			}
			else /* don't move focus on this control, but still mark it as active */
			{
				hover->state |= STATE_ACTIVE;
				sit.active = hover;
			}
			layoutUpdateStyles(sit.active);
			if ((HAS_EVT(hover, SITE_OnFocus) || HAS_EVT(sit.root, SITE_OnFocus)) && (hover->oldState & STATE_FOCUS) == 0)
				SIT_ApplyCallback(hover, (APTR) 2, SITE_OnFocus);
		}
		else if (sit.focus)
		{
			sit.focus->oldState = sit.focus->state;
			sit.focus->state &= ~(STATE_FOCUS | STATE_KBDFOCUS);
			layoutUpdateStyles(sit.focus);
			if (HAS_EVT(sit.focus, SITE_OnBlur) || HAS_EVT(sit.root, SITE_OnBlur))
				SIT_ApplyCallback(sit.focus, NULL, SITE_OnBlur);
		}
	}
	else if (! pressed && active)
	{
		sit.active = NULL;
		if (active->flags & SITF_ImmediateActive)
			return 0;
		/* toggle button XXX subclass? */
		if (! (active->type == SIT_BUTTON && ((SIT_Button)active)->type == SITV_ToggleButton && (active->state & STATE_ACTIVATED) == STATE_ACTIVATED &&
		       ! ((SIT_Button)active)->state))
		{
			active->oldState = active->state;
			active->state &= ~STATE_ACTIVE;
			layoutUpdateStyles(active);
		}
		if (sit.captureEvt)
		{
			/* check if mouse is still hovering */
			SIT_ProcessMouseMove(x, y);
			hover = sit.hover;
		}
		layoutUpdateStyles(active);
		if (active != hover)
		{
			if (hover)
			{
				hover->oldState = hover->state;
				hover->state |= STATE_HOVER;
				layoutUpdateStyles(sit.hover);
			}
		}
		else sendEvt = SIT_EventBubble(active, SITE_OnActivate);

		/* button up need to be sent to the widget processing the capture move events */
		if (sit.captureEvt)
		{
			sit.captureEvt = 0;
			hover = active;
		}
	}
	if (hover)
	{
		SIT_Widget target = SIT_EventBubble(hover, SITE_OnClick);
		if (target)
		{
			SIT_OnMouse msg = {.button = button};
			msg.state = pressed ? SITOM_ButtonPressed : SITOM_ButtonReleased;
			msg.x     = x - target->offsetX - target->layout.pos.left;
			msg.y     = y - target->offsetY - target->layout.pos.top;
			msg.flags = sit.keyQual;
			handled = SIT_ApplyCallback(target, &msg, SITE_OnClick);
			if (handled > 1 && pressed)
				sit.captureEvt = 1, sit.active = target;
			else if (handled < 0) /* cancel mouse release event */
				sit.active = NULL;
			hover = sit.hover;
			/* hidden by callback */
			if (hover && ! hover->visible)
				SIT_ProcessMouseMove(x, y);
		}
	}
	if (sendEvt && sendEvt->type != SIT_LISTBOX /* handled by listbox itself :-/ */)
	{
		if (SIT_ApplyCallback(sendEvt, active, SITE_OnActivate))
			handled = 1;
	}
	if (sit.dragCb && ! handled)
	{
		SIT_OnMouse msg = {.button = button, .state = pressed ? SITOM_ButtonPressed : SITOM_ButtonReleased, .x = sit.mouseX, .y = sit.mouseY};

		return sit.dragCb(sit.root, &msg, NULL);
	}
	return sit.hover && sit.hover != sit.root;
}

/*
 * process mouse movement
 */
static SIT_Widget SIT_CheckHoverForChildren(SIT_Widget c, REAL x, REAL y)
{
	WordWrap w;
	int      count = c->layout.wordwrap.count;
	REAL     left, top, h;
	x -= c->box.left;
	y -= c->box.top;

	if (count == 0 || (c->layout.flags & LAYF_IgnoreWords))
	{
		SIT_Widget sub;
		for (sub = HEAD(c->children); sub; NEXT(sub))
		{
			REAL box[4];
			if (! sub->visible) continue;

			memcpy(box, &sub->box, sizeof box);
			if (box[0] <= x && x < box[2] && box[1] <= y && y < box[3])
				return sub;
		}
	}
	else for (w = vector_first(c->layout.wordwrap), top = c->padding[1], left = c->padding[0], h = 0; count > 0; count --, w ++)
	{
		REAL next = left + w->width + w->space + fabsf(w->marginL) + w->marginR;
		if (w->marginL < 0) left -= w->marginL;
		if (left <= x && x < next && top <= y && y < top+w->h)
			return w->node;
		left = next;
		if (h < w->h) h = w->h;
		if (w->nl)
			left = 0, top += h, h = 0;
	}
	return c;
}

DLLIMP void SIT_ProcessMouseMove(float x, float y)
{
	SIT_Widget c, hover = sit.activeDlg;

	sit.mouseX = x;
	sit.mouseY = y;

	if (sit.dragCb)
	{
		SIT_OnMouse msg = {.state = SITOM_CaptureMove, .x = x, .y = y, .flags = sit.keyQual};

		sit.dragCb(sit.root, &msg, NULL);
	}

	if (sit.active && sit.captureEvt)
	{
		/* capture move event */
		SIT_Widget evt = SIT_EventBubble(sit.active, SITE_OnMouseMove);
		if (evt)
		{
			SIT_OnMouse msg = {.state = SITOM_CaptureMove};
			msg.flags = sit.keyQual;
			msg.x     = x - evt->offsetX - evt->layout.pos.left;
			msg.y     = y - evt->offsetY - evt->layout.pos.top;
			SIT_ApplyCallback(evt, &msg, SITE_OnMouseMove);
		}
	}
	if (sit.curTooltip && sit.curTooltip->visible && ((SIT_Tooltip)sit.curTooltip)->anchor == SITV_TooltipFollowMouse)
	{
		int XYWH[4] = {x, y, 20, 20};
		SIT_MoveNearby(sit.curTooltip, XYWH, TOOLTIP_DEFALIGN);
		sit.dirty = 1;
	}

	if (hover->type == SIT_DIALOG)
	{
		if (((SIT_Dialog)hover)->customStyles & SITV_Modal)
		{
			x -= hover->box.left;
			y -= hover->box.top;

			/* check first if outside of client area */
			if (x < 0 || y < 0 || x > hover->box.right-hover->box.left || y > hover->box.bottom-hover->box.top)
			{
				hover = NULL;
			}
		}
		else hover = sit.root; /* non-modal dialog, we can check control outside */
	}

	/* find the control being hovered: start from tail because control are ordered bottom to top (HEAD to TAIL) */
	if (hover)
	for (c = TAIL(hover->children); c; )
	{
		RectF rect;
		if (! c->visible) { PREV(c); continue; }

		memcpy(&rect, &c->box, sizeof rect);
		rect.width  -= rect.left;
		rect.height -= rect.top;
		if (c->layout.flags & LAYF_AdjustHitRect)
			c->optimalWidth(c, &rect, AdjustRenderRect);

		if (rect.left <= x && x < rect.left+rect.width && rect.top <= y && y < rect.top+rect.height && ! (c->flags & SITF_FallthroughEvt))
		{
			hover = c;
			if (c->children.lh_Head && (c->flags & SITF_PrivateChildren) == 0)
			{
				if (c->layout.wordwrap.count && (c = SIT_CheckHoverForChildren(c, x, y)))
				{
					hover = c;
					break;
				}
				x -= rect.left;
				y -= rect.top;
				c = TAIL(hover->children);
			}
			else
			{
				if (c->flags & SITF_PrivateChildren)
					hover = SIT_CheckHoverForChildren(c, x, y);
				else
					hover = c;
				break;
			}
		}
		else PREV(c);
	}
	if (hover != sit.hover)
	{
		SIT_Widget stack[10];
		int        count = 0;
		if (sit.hover)
		{
			/* exit control */
			for (c = sit.hover; c; c = c->parent)
			{
				stack[count++] = c;
				c->oldState = c->state;
				if (! ((c->flags & SITF_ToggleButon) && (c->type != SIT_BUTTON || ((SIT_Button)c)->state)))
					c->state &= ~STATE_HOVER;
			}
			if (sit.curTooltip || sit.toolTip)
			{
				SIT_SetValues(sit.curTooltip, SIT_Visible, False, NULL);
				if (sit.toolTip) SIT_ActionReschedule(sit.toolTip, -1, -1);
				sit.toolTip = NULL;
				sit.curTooltip = NULL;
			}
			c = SIT_EventBubble(sit.hover, SITE_OnMouseOut);
			if (c) SIT_ApplyCallback(c, hover, SITE_OnMouseOut);
			//fprintf(stderr, "exiting %s: [%d]\n", sit.hover->name, sit.hover->state);
		}
		if (! sit.active || sit.active == hover)
		{
			/* enter control: mark the entire stack as being hovered */
			for (c = hover; c; c = c->parent)
			{
				int i;
				for (i = 0; i < count && c != stack[i]; i ++);
				if (i == count)
				{
					c->oldState = c->state;
					stack[count++] = c;
				}
				c->state |= STATE_HOVER;
			}
			if (hover && hover->tooltip)
			{
				SIT_Tooltip tip = (SIT_Tooltip) hover->tooltip;
				if (tip->delayTime < SITV_TooltipManualTrigger)
				{
					sit.curTooltip = &tip->super;
					sit.toolTip = SIT_ActionAdd(&tip->super, sit.curTime + tip->delayTime, sit.curTime + tip->delayTime + tip->displayTime,
						SIT_TooltipTimer, NULL);
				}
			}
			#if 0
			if (hover)
			{
				fprintf(stderr, "entering %s: [%d]", hover->name, hover->state);
				if (hover->state & STATE_HOVER)  fprintf(stderr, " HOVER");
				if (hover->state & STATE_ACTIVE) fprintf(stderr, " ACTIVE");
				if (hover->state & STATE_FOCUS)  fprintf(stderr, " FOCUS");
				fputc('\n', stderr);
			}
			#endif
		}
		/* update CSS from control where state has changed */
		for (count --; count >= 0; count --)
		{
			c = stack[count];
			if (c->state != c->oldState)
				layoutUpdateStyles(c);
		}
		sit.hover = hover;
	}
	if (sit.hover)
	{
		/* regular mouse move event */
		hover = SIT_EventBubble(sit.hover, SITE_OnMouseMove);

		if (hover)
		{
			SIT_OnMouse msg = {.state = SITOM_Move};
			msg.flags = sit.keyQual;
			msg.x     = sit.mouseX - hover->offsetX - hover->layout.pos.left;
			msg.y     = sit.mouseY - hover->offsetY - hover->layout.pos.top;
			SIT_ApplyCallback(hover, &msg, SITE_OnMouseMove);
		}
	}
	/* callback registered on root node */
	if (HAS_EVT(sit.root, SITE_OnMouseMove))
	{
		hover = sit.root;
		SIT_OnMouse msg = {.state = SITOM_Move};
		msg.flags = sit.keyQual;
		msg.x     = x - hover->offsetX - hover->layout.pos.left;
		msg.y     = y - hover->offsetY - hover->layout.pos.top;
		SIT_ApplyCallback(hover, &msg, SITE_OnMouseMove);
	}
}

/*
 * screen resized
 */
DLLIMP void SIT_ProcessResize(int width, int height)
{
	SIT_Widget w = sit.root;

	if (sit.scrWidth != width || sit.scrHeight != height)
	{
		sit.scrWidth  = width;
		sit.scrHeight = height;

		w->box.right  = w->fixed.width  = w->maxBox.width  = width;
		w->box.bottom = w->fixed.height = w->maxBox.height = height;
		if ((w->style.fontSize & 3) == 3)
		{
			/* font proportionnal to viewport: recalc everything */
			w->style.font.size = cssApplyFontSize(w, w->style.fontSize);
			SIT_ChangeStyleSheet(NULL, FitUsingInitialBox);
		}
		else
		{
			layoutCalcBox(w);

			SIT_LayoutWidgets(w, KeepDialogSize);
		}
		sit.dirty = True;

		for (w = HEAD(w->children); w; NEXT(w))
		{
			if ((w->flags & SITF_TopLevel) == 0) continue;
			SIT_CenterDialog(w);
			if (w->box.right - w->box.left != w->childBox.width ||
			    w->box.bottom - w->box.top != w->childBox.height)
			{
				SIT_LayoutWidgets(w, KeepDialogSize);
			}
			else SIT_MoveWidgets(w);
		}
	}
}
