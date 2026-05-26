/*
 * Super Resolution
 * Copyright (c) 2025-2026. 187J3X1-114514
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

package io.homo.irisapi.mixin.composite.v1_21_1;

import com.google.common.collect.ImmutableList;
import com.google.common.collect.UnmodifiableIterator;
import com.mojang.blaze3d.pipeline.RenderTarget;
import io.homo.irisapi.*;
import io.homo.irisapi.handlers.IrisRenderingPipelineHandler;
import net.irisshaders.iris.pipeline.CompositeRenderer;
import org.spongepowered.asm.mixin.Final;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.Shadow;
import org.spongepowered.asm.mixin.Unique;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;
import org.spongepowered.asm.mixin.injection.callback.LocalCapture;

import java.util.ListIterator;
import java.util.Objects;

/**
 * CompositeRendererMixin for Oculus 1.8.0 (Forge 1.20.1).
 *
 * Oculus 1.8.0 is a port of an older Iris codebase. Its renderAll() uses:
 *  - An iterator-based for-each loop over passes (NOT an index for-loop)
 *  - NO GLDebug.pushGroup() / popGroup() calls
 *  - Program.unbind() once per outer iteration (before instanceof check)
 *  - FullScreenQuadRenderer.renderQuad() for non-compute passes
 *  - BlendModeOverride.restore() after quad render
 *
 * All injection targets use methods that actually exist in Oculus's renderAll(),
 * mirroring the approach from before1_21_1.CompositeRendererMixin.
 */
@Mixin(CompositeRenderer.class)
public class CompositeRendererMixin {
    #if MC_VER >= MC_1_21_1 && MC_VER <= MC_1_21_4
    @Shadow(remap = false)
    @Final
    private ImmutableList<Object> passes;

    @Unique
    private Object superresolution$getPass(int passIndex) {
        if (passIndex >= 0 && passIndex < this.passes.size()) {
            return this.passes.get(passIndex);
        }
        return null;
    }

    @Unique
    private void superresolution$handlePassEvent(int passIndex, PassEventHandler handler) {
        Object pass = superresolution$getPass(passIndex);
        Objects.requireNonNull(pass);
        handler.handle(
                new CompositeRendererAccessorImpl_After1201(((CompositeRenderer)(Object)this)),
                (NamedCompositePass) pass,
                IrisReflectionUtils.getCompositePassType(pass)
        );
    }

    // =========== PassStart ===========
    // Inject right after the outer for-each Iterator.next() assigns the next pass.
    @Inject(method = "renderAll", at = @At(
            value = "INVOKE",
            target = "Ljava/util/Iterator;next()Ljava/lang/Object;",
            ordinal = 0,
            shift = At.Shift.AFTER
    ), locals = LocalCapture.CAPTURE_FAILEXCEPTION, remap = false)
    private void onPassStart(
            CallbackInfo ci,
            RenderTarget main,
            UnmodifiableIterator<?> iter
    ) {
        int i = Math.max(((ListIterator<?>) iter).previousIndex(), 0);
        superresolution$handlePassEvent(i, IrisRenderingPipelineHandler::onCompositePassStart);
    }

    // =========== BeforeRender (compute passes) ===========
    // Oculus has NO GLDebug.pushGroup calls in renderAll(). Instead we co-inject at
    // Iterator.next() AFTER ordinal 0 (same point as PassStart) and check passType.
    // This fires before any compute programs are dispatched for this pass.
    @Inject(method = "renderAll", at = @At(
            value = "INVOKE",
            target = "Ljava/util/Iterator;next()Ljava/lang/Object;",
            ordinal = 0,
            shift = At.Shift.AFTER
    ), locals = LocalCapture.CAPTURE_FAILEXCEPTION, remap = false)
    private void onBeforeRender(
            CallbackInfo ci,
            RenderTarget main,
            UnmodifiableIterator<?> iter
    ) {
        int i = Math.max(((ListIterator<?>) iter).previousIndex(), 0);
        IrisCompositePassType passType = IrisReflectionUtils.getCompositePassType(superresolution$getPass(i));
        if (passType != IrisCompositePassType.Common) {
            superresolution$handlePassEvent(i, IrisRenderingPipelineHandler::onCompositePassDispatchBefore);
        }
    }

    // =========== BeforeRender (non-compute / quad passes) ===========
    // Fires just before the full-screen quad is drawn.
    @Inject(method = "renderAll", at = @At(
            value = "INVOKE",
            target = "Lnet/irisshaders/iris/pathways/FullScreenQuadRenderer;renderQuad()V",
            ordinal = 0
    ), locals = LocalCapture.CAPTURE_FAILEXCEPTION, remap = false)
    private void onBeforeRenderA(
            CallbackInfo ci,
            RenderTarget main,
            UnmodifiableIterator<?> iter
    ) {
        int i = Math.max(((ListIterator<?>) iter).previousIndex(), 0);
        IrisCompositePassType passType = IrisReflectionUtils.getCompositePassType(superresolution$getPass(i));
        if (passType == IrisCompositePassType.Common) {
            superresolution$handlePassEvent(i, IrisRenderingPipelineHandler::onCompositePassDispatchBefore);
        }
    }

    // =========== AfterRender (compute-only passes) ===========
    // Program.unbind() is called once per outer iteration in Oculus, BEFORE the
    // ComputeOnlyPass instanceof check. We distinguish via passType.
    @Inject(method = "renderAll", at = @At(
            value = "INVOKE",
            target = "Lnet/irisshaders/iris/gl/program/Program;unbind()V"
    ), locals = LocalCapture.CAPTURE_FAILEXCEPTION, remap = false)
    private void onAfterRender(
            CallbackInfo ci,
            RenderTarget main,
            UnmodifiableIterator<?> iter
    ) {
        int i = Math.max(((ListIterator<?>) iter).previousIndex(), 0);
        IrisCompositePassType passType = IrisReflectionUtils.getCompositePassType(superresolution$getPass(i));
        if (passType == IrisCompositePassType.ComputeOnly) {
            superresolution$handlePassEvent(i, IrisRenderingPipelineHandler::onCompositePassDispatchAfter);
        }
    }

    // =========== AfterRender (non-compute / quad passes) ===========
    // Fires immediately after the full-screen quad has been drawn.
    @Inject(method = "renderAll", at = @At(
            value = "INVOKE",
            target = "Lnet/irisshaders/iris/pathways/FullScreenQuadRenderer;renderQuad()V",
            shift = At.Shift.AFTER
    ), locals = LocalCapture.CAPTURE_FAILEXCEPTION, remap = false)
    private void onAfterRenderA(
            CallbackInfo ci,
            RenderTarget main,
            UnmodifiableIterator<?> iter
    ) {
        int i = Math.max(((ListIterator<?>) iter).previousIndex(), 0);
        IrisCompositePassType passType = IrisReflectionUtils.getCompositePassType(superresolution$getPass(i));
        if (passType != IrisCompositePassType.ComputeOnly) {
            superresolution$handlePassEvent(i, IrisRenderingPipelineHandler::onCompositePassDispatchAfter);
        }
    }

    // =========== PassEnd (compute-only passes) ===========
    // Oculus has NO GLDebug.popGroup() in renderAll(). Instead, Program.unbind() AFTER
    // is the last shared call before ComputeOnlyPass hits `continue`. We check passType.
    @Inject(method = "renderAll", at = @At(
            value = "INVOKE",
            target = "Lnet/irisshaders/iris/gl/program/Program;unbind()V",
            shift = At.Shift.AFTER,
            ordinal = 0
    ), locals = LocalCapture.CAPTURE_FAILEXCEPTION, remap = false)
    private void onPassEnd(
            CallbackInfo ci,
            RenderTarget main,
            UnmodifiableIterator<?> iter
    ) {
        int i = Math.max(((ListIterator<?>) iter).previousIndex(), 0);
        IrisCompositePassType passType = IrisReflectionUtils.getCompositePassType(superresolution$getPass(i));
        if (passType == IrisCompositePassType.ComputeOnly) {
            superresolution$handlePassEvent(i, IrisRenderingPipelineHandler::onCompositePassEnd);
        }
    }

    // =========== PassEnd (non-compute / quad passes) ===========
    // Oculus has NO GLDebug.popGroup() in renderAll(). BlendModeOverride.restore()
    // is called only for non-ComputeOnly passes, making it the correct end-of-pass hook.
    @Inject(method = "renderAll", at = @At(
            value = "INVOKE",
            target = "Lnet/irisshaders/iris/gl/blending/BlendModeOverride;restore()V",
            shift = At.Shift.AFTER,
            ordinal = 0
    ), locals = LocalCapture.CAPTURE_FAILEXCEPTION, remap = false)
    private void onPassEndA(
            CallbackInfo ci,
            RenderTarget main,
            UnmodifiableIterator<?> iter
    ) {
        int i = Math.max(((ListIterator<?>) iter).previousIndex(), 0);
        IrisCompositePassType passType = IrisReflectionUtils.getCompositePassType(superresolution$getPass(i));
        if (passType != IrisCompositePassType.ComputeOnly) {
            superresolution$handlePassEvent(i, IrisRenderingPipelineHandler::onCompositePassEnd);
        }
    }
    #endif
}
