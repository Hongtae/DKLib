//
//  File: DKFrame.cpp
//  Author: Hongtae Kim (tiff2766@gmail.com)
//
//  Copyright (c) 2004-2020 Hongtae Kim. All rights reserved.
//

#include "DKMath.h"
#include "DKFrame.h"
#include "DKScreen.h"
#include "DKVector2.h"
#include "DKAffineTransform2.h"
#include "DKCanvas.h"
#include "DKTexture.h"

using namespace DKFramework;

DKFrame::DKFrame()
    : transform(DKMatrix3::identity)
    , transformInverse(DKMatrix3::identity)
    , superframe(NULL)
    , screen(NULL)
    , loaded(false)
    , drawSurface(false)
    , contentResolution(DKSize(1, 1))
    , contentScale(1, 1)
    , contentTransform(DKMatrix3::identity)
    , contentTransformInverse(DKMatrix3::identity)
    , color(DKColor(1, 1, 1, 1).RGBA32Value())
    , blendState(DKBlendState::defaultOpaque)
    , hidden(false)
    , enabled(true)
    , pixelFormat(DKPixelFormat::RGBA8Unorm)
{
}

DKFrame::~DKFrame()
{
    if (IsLoaded()) //frame was not unloaded.
    {
        DKLogW("The frame is being destroyed, but it is loaded and OnUnload() will not be called.");
        this->Unload();
    }

    RemoveFromSuperframe();

    decltype(subframes) frames = subframes;
    for (DKFrame* frame : frames)
    {
        frame->RemoveFromSuperframe();
    }
}

void DKFrame::Load(DKScreen* screen, const DKSize& resolution)
{
    if (this->screen != screen)
    {
        Unload();
        this->screen = screen;
        if (this->screen)
        {
            DKSize res = resolution;
            if (res.width < 1.0f)
                res.width = 1.0f;
            if (res.height < 1.0f)
                res.height = 1.0f;

            this->contentResolution = res;
            this->contentResolution = this->DKFrame::CalculateContentResolution();
            this->OnLoaded();
            this->loaded = true;
            this->OnContentResized();
            this->UpdateContentResolution();

            decltype(subframes) frames = subframes;
            for (DKFrame* frame : frames)
            {
                frame->Load(screen, resolution);
            }
            SetRedraw();
        }
    }
}

void DKFrame::Unload()
{
    decltype(subframes) frames = subframes;
    for (DKFrame* frame : frames)
    {
        frame->Unload();
    }

    if (this->loaded)
    {
        this->screen->LeaveHoverFrame(this);
        this->screen->RemoveFocusFrameForAnyDevices(this, false);
        this->screen->RemoveKeyFrameForAnyDevices(this, false);

        this->OnUnload();
        this->renderTarget = nullptr;
    }
    //this->contentResolution = DKSize(1,1);
    this->screen = NULL;
    this->loaded = false;
}

bool DKFrame::AddSubframe(DKFrame* frame)
{
    if (frame && frame->superframe == nullptr && !this->IsDescendantOf(frame))
    {
        //this->subframes.Add(frame);
        this->subframes.Insert(frame, 0);	// bring to front
        frame->superframe = this;
        if (this->loaded)
        {
            frame->Load(this->screen, this->ContentResolution());
            frame->UpdateContentResolution();
            SetRedraw();
        }
        return true;
    }
    return false;
}

void DKFrame::RemoveSubframe(DKFrame* frame)
{
    if (frame && frame->superframe == this)
    {
        if (frame->screen)
        {
            frame->screen->LeaveHoverFrame(frame);
            frame->screen->RemoveFocusFrameForAnyDevices(frame, true);
            frame->screen->RemoveKeyFrameForAnyDevices(frame, true);
        }

        for (uint32_t index = 0; index < subframes.Count(); ++index)
        {
            if (subframes.Value(index) == frame)
            {
                DKObject<DKFrame> temp = frame; // take ownership (temporary)

                frame->superframe = nullptr;
                subframes.Remove(index); // object can be destroyed now
                SetRedraw();
                return;
            }
        }
    }
}

void DKFrame::RemoveFromSuperframe()
{
    if (superframe)
        superframe->RemoveSubframe(this);
}

bool DKFrame::BringSubframeToFront(DKFrame* frame)
{
    if (frame && frame->superframe == this)
    {
        for (uint32_t index = 0; index < this->subframes.Count(); ++index)
        {
            if (this->subframes.Value(index) == frame)
            {
                if (index > 0)
                {
                    DKObject<DKFrame> temp = frame; // take ownership (temporary)

                    this->subframes.Remove(index);
                    this->subframes.Insert(frame, 0);
                    this->SetRedraw();
                }
                return true;
            }
        }
    }
    return false;
}

bool DKFrame::SendSubframeToBack(DKFrame* frame)
{
    if (frame && frame->superframe == this)
    {
        if (screen)
            screen->LeaveHoverFrame(this);

        for (uint32_t index = 0; index < this->subframes.Count(); ++index)
        {
            if (this->subframes.Value(index) == frame)
            {
                if (index + 1 < this->subframes.Count())
                {
                    DKObject<DKFrame> temp = frame; // take ownership (temporary)

                    this->subframes.Remove(index);
                    this->subframes.Add(frame);
                    this->SetRedraw();
                }
                return true;
            }
        }
    }
    return false;
}

DKFrame* DKFrame::SubframeAtIndex(uint32_t index)
{
    if (index < subframes.Count())
        return subframes.Value(index);
    return nullptr;
}

const DKFrame* DKFrame::SubframeAtIndex(uint32_t index) const
{
    if (index < subframes.Count())
        return subframes.Value(index);
    return nullptr;
}

bool DKFrame::IsDescendantOf(const DKFrame* frame) const
{
    if (frame == NULL)
        return false;
    if (frame == this)
        return true;
    if (superframe == NULL)
        return false;

    return superframe->IsDescendantOf(frame);
}

size_t DKFrame::NumberOfSubframes() const
{
    return subframes.Count();
}

size_t DKFrame::NumberOfDescendants() const
{
    size_t num = 1;
    for (size_t i = 0; i < subframes.Count(); ++i)
        num += subframes.Value(i)->NumberOfDescendants();
    return num;
}

void DKFrame::SetTransform(const DKMatrix3& transform)
{
    if (screen && screen->RootFrame() == this)
    {
        DKLog("RootFrame's transform cannot be changed.\n");
    }
    else
    {
        if (this->transform != transform)
        {
            this->transform = transform;
            this->transformInverse = DKMatrix3(transform).Inverse();

            this->UpdateContentResolution();

            if (this->superframe)
                superframe->SetRedraw();
        }
    }
}

DKMatrix3 DKFrame::LocalFromRootTransform() const
{
    DKMatrix3 tm = superframe ? superframe->LocalFromRootTransform() : DKMatrix3::identity;
    return tm * this->LocalFromSuperTransform();
}

DKMatrix3 DKFrame::LocalToRootTransform() const
{
    DKMatrix3 tm = superframe ? superframe->LocalToRootTransform() : DKMatrix3::identity;
    return this->LocalToSuperTransform() * tm;
}

DKMatrix3 DKFrame::LocalFromSuperTransform() const
{
    DKMatrix3 tm = DKMatrix3::identity;
    if (superframe)
    {
        // to normalized local coordinates.
        tm.Multiply(this->transformInverse);

        // apply local content scale.
        tm.Multiply(DKAffineTransform2(DKLinearTransform2().Scale(contentScale.width, contentScale.height)).Matrix3());

        // apply inversed content transform.
        tm.Multiply(this->contentTransformInverse);
    }
    return tm;
}

DKMatrix3 DKFrame::LocalToSuperTransform() const
{
    DKMatrix3 tm = DKMatrix3::identity;
    if (superframe)
    {
        // apply content transform.
        tm.Multiply(this->contentTransform);

        // normalize local scale to (0.0 ~ 1.0)
        tm.Multiply(DKAffineTransform2(DKLinearTransform2().Scale(1.0f / contentScale.width, 1.0f / contentScale.height)).Matrix3());

        // transform to parent
        tm.Multiply(this->transform);
    }
    return tm;
}

void DKFrame::UpdateContentResolution()
{
    if (!this->loaded)
        return;

    bool resized = false;
    if (screen && screen->RootFrame() == this)
    {
        DKSize size = screen->Resolution();
        DKASSERT_DEBUG(size.width > 0.0f && size.height > 0.0f);

        uint32_t width =  Max<uint32_t>(floor(size.width + 0.5f), 1U);
        uint32_t height = Max<uint32_t>(floor(size.height + 0.5f), 1U);
        if ((uint32_t)floor(contentResolution.width + 0.5f) != width || (uint32_t)floor(contentResolution.height + 0.5f) != height)
        {
            //DKLog("DKFrame (0x%x, root) resized. (%dx%d -> %dx%d)\n", this, (int)contentResolution.width, (int)contentResolution.height, width, height);
            resized = true;
            contentResolution.width = width;
            contentResolution.height = height;
        }
    }
    else
    {
        DKSize size = CalculateContentResolution();

        uint32_t maxTexSize = 1 << 14; //DKTexture::MaxTextureSize();
        uint32_t width = Clamp<uint32_t>(floor(size.width + 0.5f), 1U, maxTexSize);
        uint32_t height = Clamp<uint32_t>(floor(size.height + 0.5f), 1U, maxTexSize);

        if ((uint32_t)floor(contentResolution.width + 0.5f) != width || (uint32_t)floor(contentResolution.height + 0.5f) != height)
        {
            //DKLog("DKFrame (0x%x) resized. (%dx%d -> %dx%d)\n", this, (int)contentResolution.width, (int)contentResolution.height, width, height);
            resized = true;
            contentResolution.width = width;
            contentResolution.height = height;

            this->DiscardSurface();
        }
    }

    DKASSERT_DEBUG(this->contentResolution.width > 0.0f && this->contentResolution.height > 0.0f);

    if (resized)
    {
        OnContentResized();
        SetRedraw();
    }

    decltype(subframes) frames = subframes;
    for (DKFrame* frame : frames)
    {
        frame->UpdateContentResolution();
    }
}

DKSize DKFrame::CalculateContentResolution() const
{
    if (superframe)
    {
        DKSize superRes = superframe->ContentResolution();
        if (superRes.width > 0 && superRes.height > 0)
        {
            // get each points of box (not rect)
            float w = contentScale.width;
            float h = contentScale.height;

            DKPoint lt = superframe->LocalToPixel(this->LocalToSuper(DKPoint(0, 0)));   // left-top
            DKPoint rt = superframe->LocalToPixel(this->LocalToSuper(DKPoint(w, 0)));   // right-top
            DKPoint lb = superframe->LocalToPixel(this->LocalToSuper(DKPoint(0, h)));   // left-bottom
            DKPoint rb = superframe->LocalToPixel(this->LocalToSuper(DKPoint(w, h)));   // right-bottom

            DKVector2 horizontal1 = rb.Vector() - lb.Vector();		// vertical length 1
            DKVector2 horizontal2 = rt.Vector() - lt.Vector();		// vertical length 2
            DKVector2 vertical1 = lt.Vector() - lb.Vector();		// horizontal length 1
            DKVector2 vertical2 = rt.Vector() - rb.Vector();		// horizontal length 2

            DKSize result = DKSize(Max<float>(horizontal1.Length(), horizontal2.Length()), Max<float>(vertical1.Length(), vertical2.Length()));
            // round to be int
            result.width = floor(result.width + 0.5f);
            result.height = floor(result.height + 0.5f);
            return result;
        }
    }
    return contentResolution;
}

bool DKFrame::CaptureKeyboard(int deviceId)
{
    if (screen && this->CanHandleKeyboard())
        return screen->SetKeyFrame(deviceId, this);
    return false;
}

bool DKFrame::CaptureMouse(int deviceId)
{
    if (screen && this->CanHandleMouse())
        return screen->SetFocusFrame(deviceId, this);
    return false;
}

void DKFrame::ReleaseKeyboard(int deviceId)
{
    if (screen)
        screen->RemoveKeyFrame(deviceId, this);
}

void DKFrame::ReleaseMouse(int deviceId)
{
    if (screen)
        screen->RemoveFocusFrame(deviceId, this);
}

void DKFrame::ReleaseAllKeyboardsCapturedBySelf()
{
    if (screen)
        screen->RemoveKeyFrameForAnyDevices(this, false);
}

void DKFrame::ReleaseAllMiceCapturedBySelf()
{
    if (screen)
        screen->RemoveFocusFrameForAnyDevices(this, false);
}

bool DKFrame::IsKeyboardCapturedBySelf(int deviceId) const
{
    if (screen && this->IsDescendantOf(screen->RootFrame()))
        return screen->KeyFrame(deviceId) == this;
    return false;
}

bool DKFrame::IsMouseCapturedBySelf(int deviceId) const
{
    if (screen && this->IsDescendantOf(screen->RootFrame()))
            return screen->FocusFrame(deviceId) == this;
    return false;
}

DKPoint DKFrame::MousePosition(int deviceId) const
{
    if (screen && screen->Window())
    {
        const DKWindow* window = screen->Window();
        const DKFrame* rootFrame = screen->RootFrame();
        if (IsDescendantOf(rootFrame))
        {
            // transform mouse pos to screen coordinates (0.0 ~ 1.0)
            DKVector2 pos = screen->WindowToScreen(window->MousePosition(deviceId)).Vector();

            // convert to root-frame's coordinates
            const DKSize& scale = rootFrame->contentScale;
            DKMatrix3 tm = DKMatrix3::identity;
            tm.Multiply(DKAffineTransform2(DKLinearTransform2(scale.width, scale.height)).Matrix3());
            tm.Multiply(rootFrame->contentTransformInverse);

            // convert to local (this) coordinates.
            tm.Multiply(this->LocalFromRootTransform());
            return pos.Transform(tm);
        }
    }
    return DKPoint(-1, -1);
}

bool DKFrame::IsMouseHover(int deviceId) const
{
    if (screen)
        return screen->HoverFrame(deviceId) == this;
    return false;
}

DKSize DKFrame::ContentResolution() const
{
    return contentResolution;
}

DKSize DKFrame::ContentScale() const
{
    return contentScale;
}

void DKFrame::SetContentScale(const DKSize& s)
{
    float w = Max(s.width, DKCanvas::minimumScaleFactor);
    float h = Max(s.height, DKCanvas::minimumScaleFactor);

    if (fabsf(w - contentScale.width) < FLT_EPSILON &&
        fabsf(h - contentScale.height) < FLT_EPSILON)
    {
    }
    else
    {
        contentScale = DKSize(w, h);
        SetRedraw();
    }
}

DKRect DKFrame::Bounds() const
{
    return DKRect(0, 0, contentScale.width, contentScale.height);
}

DKRect DKFrame::DisplayBounds() const
{
    DKRect rc = this->Bounds();
    DKVector2 v0(rc.origin.x, rc.origin.y);
    DKVector2 v1(rc.origin.x, rc.origin.y + rc.size.height);
    DKVector2 v2(rc.origin.x + rc.size.width, rc.origin.y);
    DKVector2 v3(rc.origin.x + rc.size.width, rc.origin.y + rc.size.height);

    v0.Transform(this->contentTransformInverse);
    v1.Transform(this->contentTransformInverse);
    v2.Transform(this->contentTransformInverse);
    v3.Transform(this->contentTransformInverse);

    DKVector2 minp = v0;
    DKVector2 maxp = v0;

    for (const DKVector2& v : { v1, v2, v3 })
    {
        if (minp.x > v.x)		minp.x = v.x;
        if (minp.y > v.y)		minp.y = v.y;
        if (maxp.x < v.x)		maxp.x = v.x;
        if (maxp.y < v.y)		maxp.y = v.y;
    }
    return DKRect(minp, maxp - minp);
}

void DKFrame::SetContentTransform(const DKMatrix3& m)
{
    if (this->contentTransform != m)
    {
        bool invertible = false;
        DKMatrix3 inv = m.InverseMatrix(&invertible);
        if (invertible)
        {
            this->contentTransform = m;
            this->contentTransformInverse = inv;
        }
        else
        {
            this->contentTransform = DKMatrix3::identity;
            this->contentTransformInverse = DKMatrix3::identity;
        }
        this->SetRedraw();
    }
}

const DKMatrix3& DKFrame::ContentTransform() const
{
    return this->contentTransform;
}

const DKMatrix3& DKFrame::ContentTransformInverse() const
{
    return this->contentTransformInverse;
}

DKPoint DKFrame::LocalToSuper(const DKPoint& pt) const
{
    if (superframe)
    {
        DKASSERT_DEBUG(contentScale.width > 0.0f && contentScale.height > 0.0f);

        DKVector2 v = pt.Vector();

        // apply content transform.
        v.Transform(this->contentTransform);

        // normalize coordinates (0.0 ~ 1.0)
        v.x = v.x / this->contentScale.width;
        v.y = v.y / this->contentScale.height;

        // transform to parent.
        v.Transform(this->transform);

        return v;
    }
    return pt;
}

DKPoint DKFrame::SuperToLocal(const DKPoint& pt) const
{
    if (superframe)
    {
        DKVector2 v = pt.Vector();

        // apply inversed transform to normalize coordinates (0.0 ~ 1.0)
        v.Transform(this->transformInverse);

        // apply content scale
        v.x = v.x * this->contentScale.width;
        v.y = v.y * this->contentScale.height;

        // apply inversed content transform
        v.Transform(this->contentTransformInverse);

        return v;
    }
    return pt;
}

DKPoint DKFrame::LocalToPixel(const DKPoint& pt) const
{
    DKASSERT_DEBUG(contentScale.width > 0.0f && contentScale.height > 0.0f);

    DKVector2 v = pt.Vector();

    // apply content transform.
    v.Transform(this->contentTransform);

    // normalize coordinates (0.0 ~ 1.0)
    v.x = v.x / this->contentScale.width;
    v.y = v.y / this->contentScale.height;

    // convert to pixel-space.
    v.x = v.x * this->contentResolution.width;
    v.y = v.y * this->contentResolution.height;

    return v;
}

DKPoint DKFrame::PixelToLocal(const DKPoint& pt) const
{
    DKASSERT_DEBUG(this->contentResolution.width > 0.0f && this->contentResolution.height > 0.0f);

    DKVector2 v = pt.Vector();

    // normalize coordinates.
    v.x = v.x / this->contentResolution.width;
    v.y = v.y / this->contentResolution.height;

    // apply content scale.
    v.x = v.x * this->contentScale.width;
    v.y = v.y * this->contentScale.height;

    // apply inversed content transform.
    v.Transform(this->contentTransformInverse);

    return v;
}

DKSize DKFrame::LocalToPixel(const DKSize& size) const
{
    DKPoint p0 = LocalToPixel(DKPoint(0, 0));
    DKPoint p1 = LocalToPixel(DKPoint(size.width, size.height));

    return DKSize(p1.x - p0.x, p1.y - p0.y);
}

DKSize DKFrame::PixelToLocal(const DKSize& size) const
{
    DKPoint p0 = PixelToLocal(DKPoint(0, 0));
    DKPoint p1 = PixelToLocal(DKPoint(size.width, size.height));

    return DKSize(p1.x - p0.x, p1.y - p0.y);
}

DKRect DKFrame::LocalToPixel(const DKRect& rect) const
{
    return DKRect(LocalToPixel(rect.origin), LocalToPixel(rect.size));
}

DKRect DKFrame::PixelToLocal(const DKRect& rect) const
{
    return DKRect(PixelToLocal(rect.origin), PixelToLocal(rect.size));
}

void DKFrame::Update(double tickDelta, DKTimeTick tick, const DKDateTime& tickDate)
{
    DKASSERT_DESC_DEBUG(IsLoaded(), "Frame must be initialized with screen!");

    OnUpdate(tickDelta, tick, tickDate);

    decltype(subframes) frames = subframes;
    for (DKFrame* frame : frames)
    {
        frame->Update(tickDelta, tick, tickDate);
    }
}

void DKFrame::Draw() const
{
    const_cast<DKFrame*>(this)->DrawInternal();
}

bool DKFrame::DrawInternal()
{
    DKASSERT_DESC_DEBUG(IsLoaded(), "Frame must be initialized with screen!");
    DKASSERT_DEBUG(this->contentResolution.width > 0.0f && this->contentResolution.height > 0.0f);

    bool drawSelf = false;
    const DKRect bounds = this->Bounds();
    for (DKFrame* frame : subframes)
    {
        if (frame->IsHidden())
            continue;
        // check frame inside. (visibility)
        bool covered = false;
        if (frame->InsideFrameRect(&covered, bounds,
                                   this->contentTransform,
                                   this->contentTransformInverse) == false)	// frame is not visible.
            continue;

        if (this->drawSurface && frame->renderTarget == nullptr)
            frame->SetRedraw();

        if (frame->DrawInternal())
            drawSelf = true;  // redraw self if child has been drawn
    }

    if (drawSelf || this->drawSurface)
    {
        DKObject<DKCanvas> canvas = nullptr;
        if (screen && screen->RootFrame() == this)
        {
            canvas = screen->CreateCanvas();
            DKASSERT_DEBUG(canvas != nullptr);
            this->renderTarget = nullptr;
        }
        else
        {
            if (renderTarget == nullptr)
            {
                if (screen)
                {
                    DKASSERT_DEBUG(screen->RootFrame() != this);

                    DKGraphicsDeviceContext* deviceContext = screen->GraphicsDevice();
                    DKASSERT_DEBUG(deviceContext);

                    // create render target
                    int width = floor(this->contentResolution.width + 0.5f);
                    int height = floor(this->contentResolution.height + 0.5f);

                    DKASSERT_DEBUG(DKPixelFormatIsColorFormat(this->pixelFormat));

                    DKTextureDescriptor desc = {};
                    desc.textureType = DKTexture::Type2D;
                    desc.pixelFormat = this->pixelFormat;
                    desc.width = width;
                    desc.height = height;
                    desc.depth = 1;
                    desc.mipmapLevels = 1;
                    desc.sampleCount = 1;
                    desc.arrayLength = 1;
                    desc.usage = DKTexture::UsageSampled | DKTexture::UsageRenderTarget;

                    this->renderTarget = deviceContext->Device()->CreateTexture(desc);
                    this->drawSurface = true;
                    DKLogI("Create render-target (%dx%d) for DKFrame:0x%x", width, height, this);
                }
                else
                {
                    DKLogE("Cannot create render-target, Screen is null");
                }
            }
            if (renderTarget && canvas == nullptr)
            {
                DKCommandQueue* queue = nullptr;
                if (screen)
                    queue = screen->CommandQueue();
                if (queue)
                {
                    // create canvas from renderTarget..
                    DKCommandBuffer* buffer = queue->CreateCommandBuffer();
                    canvas = DKOBJECT_NEW DKCanvas(buffer, renderTarget);
                }
            }
        }
        if (canvas)
        {
            canvas->SetViewport(DKRect(0, 0, contentResolution.width, contentResolution.height));
            canvas->SetContentBounds(DKRect(0, 0, this->contentScale.width, this->contentScale.height));
            canvas->SetContentTransform(this->contentTransform);

            // draw background
            this->OnDraw(canvas);

            // draw frame reverse order
            for (long i = (long)subframes.Count() - 1; i >= 0; --i)
            {
                const DKFrame* frame = subframes.Value(i);
                if (frame->IsHidden())
                    continue;

                // draw texture
                const DKTexture* texture = frame->renderTarget;
                if (texture)
                {
                    canvas->DrawRect(DKRect(0, 0, 1, 1), frame->Transform(),
                                     DKRect(0, 0, 1, 1), DKMatrix3::identity,
                                     texture,
                                     frame->color,
                                     frame->blendState);
                }
            }
            // draw overlay
            this->OnDrawOverlay(canvas);
            canvas->Commit();

            this->drawSurface = false;
            return true;
        }
    }
    return false;
}

bool DKFrame::InsideFrameRect(bool* covered, const DKRect& rect, const DKMatrix3& tm, const DKMatrix3& invTm) const
{
    DKVector2 outerPos[4] = {
        DKVector2(rect.origin.x, rect.origin.y),										// left-top
        DKVector2(rect.origin.x, rect.origin.y + rect.size.height),						// left-bottom
        DKVector2(rect.origin.x + rect.size.width, rect.origin.y + rect.size.height),	// right-bottom
        DKVector2(rect.origin.x + rect.size.width, rect.origin.y)						// right-top
    };

    // apply parent space transform.
    DKMatrix3 m = invTm * this->transformInverse;
    for (DKVector2& v : outerPos)
        v.Transform(m);

    // check outPos is inside rect.
    if (outerPos[0].x >= 0.0 && outerPos[0].x <= 1.0 && outerPos[0].y >= 0.0 && outerPos[0].y <= 1.0 &&
        outerPos[1].x >= 0.0 && outerPos[1].x <= 1.0 && outerPos[1].y >= 0.0 && outerPos[1].y <= 1.0 &&
        outerPos[2].x >= 0.0 && outerPos[2].x <= 1.0 && outerPos[2].y >= 0.0 && outerPos[2].y <= 1.0 &&
        outerPos[3].x >= 0.0 && outerPos[3].x <= 1.0 && outerPos[3].y >= 0.0 && outerPos[3].y <= 1.0)
    {
        // frame rect covered parent frame entirely.
        if (covered)
            *covered = true;
        return true;
    }

    if (covered)
        *covered = false;

    // check normalized local rect(0,0,1,1) is inside parent rect.
    return rect.IntersectRect(DKRect(0, 0, 1, 1), this->transform * tm);
}

const DKTexture* DKFrame::Texture() const
{
    return renderTarget;
}

void DKFrame::SetRedraw() const
{
    drawSurface = true;
}

void DKFrame::DiscardSurface()
{
    renderTarget = nullptr;		// create on render
    SetRedraw();
}

void DKFrame::SetColor(const DKColor& color)
{
    if (screen && screen->RootFrame() == this)
    {
        DKLog("RootFrame's color cannot be changed.\n");
    }
    else
    {
        this->color = color.RGBA32Value();
        if (this->superframe)
            superframe->SetRedraw();
    }
}

void DKFrame::SetBlendState(const DKBlendState& blend)
{
    if (screen && screen->RootFrame() == this)
    {
        DKLog("RootFrame's blend state cannot be changed.\n");
    }
    else
    {
        this->blendState = blend;
        if (this->superframe)
            superframe->SetRedraw();
    }
}

const DKBlendState& DKFrame::BlendState() const
{
    return blendState;
}

bool DKFrame::ProcessKeyboardEvent(const DKWindow::KeyboardEvent& event)
{
    // call PreprocessKeyboardEvent() from top level object to this.
    // if function returns true, stop precessing. (intercepted)
    struct Preprocess
    {
        DKFrame* frame;
        bool operator () (const DKWindow::KeyboardEvent& ep)
        {
            if (frame->superframe)
            {
                Preprocess parent = { frame->superframe };
                if (parent(ep))
                    return true;
            }
            return frame->PreprocessKeyboardEvent(ep);
        }
    };

    Preprocess pre = { this };
    if (pre(event))
        return true;

    if (this->CanHandleKeyboard())
    {
        OnKeyboardEvent(event);
        return true;
    }
    return false;
}

bool DKFrame::ProcessMouseEvent(const DKWindow::MouseEvent& event, const DKPoint& pos, const DKVector2& delta, bool propagate)
{
    // apply content scale, transform with normalized coordinates. (pos is normalized)
    DKVector2 localPos = DKVector2(pos.x * this->contentScale.width, pos.y * this->contentScale.height);
    localPos.Transform(this->contentTransformInverse);

    DKVector2 localPosOld = DKVector2((pos.x - delta.x) * this->contentScale.width, (pos.y - delta.y) * this->contentScale.height);
    localPosOld.Transform(this->contentTransformInverse);

    DKVector2 localDelta = localPos - localPosOld;

    // call PreprocessMouseEvent() from top level object to this.
    // if function returns true, stop precessing. (intercepted)
    struct Preprocess
    {
        DKFrame* frame;
        bool operator () (const DKWindow::MouseEvent& event, const DKPoint& pos, const DKPoint& posOld)
        {
            if (frame->superframe)
            {
                DKPoint pos2 = frame->LocalToSuper(pos);
                DKPoint posOld2 = frame->LocalToSuper(posOld);

                Preprocess parent = { frame->superframe };
                if (parent(event, pos2, posOld2))
                    return true;
            }
            return frame->PreprocessMouseEvent(event, pos, (pos - posOld).Vector());
        }
    };

    if (propagate)
    {
        if (!this->HitTest(localPos))
            return false;

        if (this->ContentHitTest(localPos))
        {
            for (DKFrame* frame : subframes)
            {
                if (frame->IsHidden())
                    continue;

                // apply inversed frame transform (convert to normalized frame coordinates)
                DKMatrix3 tm = frame->TransformInverse();
                DKVector2 posInFrame = DKVector2(localPos).Transform(tm);

                if (DKRect(0, 0, 1, 1).IsPointInside(posInFrame))
                {
                    DKVector2 oldPosInFrame = DKVector2(localPosOld).Transform(tm);
                    DKVector2 deltaInFrame = posInFrame - oldPosInFrame;

                    // send event to frame whether it is able to process or not. (frame is visible-destionation)
                    if (frame->ProcessMouseEvent(event, posInFrame, deltaInFrame, propagate))
                        return true;
                }
            }
        }
    }
    else
    {
        // call PreprocessMouseEvent() from top level object to this.
        // if function returns true, stop precessing. (intercepted)
        Preprocess pre = { this };
        if (pre(event, localPos, localPosOld))
            return true;
    }

    // no frames can handle event. should process by this.
    // or mouse has been captured by this.
    if (this->CanHandleMouse())
    {
        if (propagate)
        {
            // call PreprocessMouseEvent() from top level object to this.
            // if function returns true, stop precessing. (intercepted)
            Preprocess pre = { this };
            if (pre(event, localPos, localPosOld))
                return true;
        }

        OnMouseEvent(event, localPos, localDelta);
        return true;
    }
    return false;
}

DKFrame* DKFrame::FindHoverFrame(const DKPoint& pos)
{
    if (!this->hidden)
    {
        if (DKRect(0, 0, 1, 1).IsPointInside(pos))
        {
            DKVector2 localPos = DKVector2(pos.x * this->contentScale.width, pos.y * this->contentScale.height);
            localPos.Transform(this->contentTransformInverse);

            if (this->HitTest(localPos))
            {
                if (this->ContentHitTest(localPos))
                {
                    for (DKFrame* frame : subframes)
                    {
                        DKMatrix3 tm = frame->TransformInverse();
                            DKFrame* hover = frame->FindHoverFrame(DKVector2(localPos).Transform(tm));
                            if (hover)
                                return hover;
                    }
                }

                if (this->CanHandleMouse())
                    return this;
            }
        }
    }
    return nullptr;
}

void DKFrame::SetHidden(bool hidden)
{
    if (screen && screen->RootFrame() == this)
    {
        DKLog("RootFrame is always visible.\n");
        return;
    }

    if (this->hidden != hidden)
    {
        this->hidden = hidden;
        if (this->hidden)
        {
            if (screen)
                screen->LeaveHoverFrame(this);
        }
        if (this->superframe)
            superframe->SetRedraw();
    }
}

void DKFrame::SetEnabled(bool enabled)
{
    if (this->enabled != enabled)
    {
        this->enabled = enabled;
        if (!this->enabled)
        {
            if (screen)
                screen->LeaveHoverFrame(this);
        }
        SetRedraw();
    }
}

bool DKFrame::CanHandleKeyboard() const
{
    return this->IsEnabled() && this->UserInputEventEnabled();
}

bool DKFrame::CanHandleMouse() const
{
    return this->IsEnabled() && this->IsVisibleOnScreen() && this->UserInputEventEnabled();
}

bool DKFrame::IsVisibleOnScreen() const
{
    if (screen == NULL)
        return false;

    if (screen->RootFrame() == this)
        return true;

    if (this->hidden)
        return false;

    if (superframe)
        return superframe->IsVisibleOnScreen();

    return false;
}

void DKFrame::SetPixelFormat(DKPixelFormat fmt)
{
    if (screen && screen->RootFrame() == this)
    {
        DKLogE("The pixel format setting of the root frame has not yet been implemented.");
    }
    else if (pixelFormat != fmt)
    {
        if (DKPixelFormatIsColorFormat(fmt))
        {
            pixelFormat = fmt;
            renderTarget = nullptr;		// create on render
            SetRedraw();
        }
        else
        {
            DKLogE("PixelFormat: 0x%x is not a valid color format", (uint32_t)fmt);
        }
    }
}

DKPixelFormat DKFrame::PixelFormat() const
{
    return pixelFormat;
}

void DKFrame::OnDraw(DKCanvas* canvas) const
{
    canvas->Clear(DKColor(1, 1, 1, 1));
}
