// Internal MLK, STD, and content-graph operation helpers. Included by OperationExecution.cpp.

int executeMlkCorpus(
    const spice::mix::ExportMlkCorpusRequest& request,
    spice::mix::OperationContext& context) {
    std::filesystem::create_directories(request.output);
    emit(context, spice::mix::EventLevel::Progress,
        "Step 2/4: Scanning MLK corpus.");
    const auto corpus = spice::mlk::scanMlkCorpus(request.input);
    emit(context, spice::mix::EventLevel::Progress,
        "Step 3/4: Writing MLK corpus artifacts.");
    const auto written = spice::mlk::writeMlkCorpusArtifacts(corpus, request.output);
    emit(context, spice::mix::EventLevel::Info, "  - Wrote ", written.jsonPath.string());
    emit(context, spice::mix::EventLevel::Info, "  - Wrote ", written.filesCsvPath.string());
    emit(context, spice::mix::EventLevel::Info, "  - Wrote ", written.recordsCsvPath.string());
    emit(context, spice::mix::EventLevel::Info, "  - Wrote ", written.word12HistogramCsvPath.string());
    emit(context, spice::mix::EventLevel::Info, "  - Wrote ", written.anomaliesCsvPath.string());
    emit(context, spice::mix::EventLevel::Info, "  - Wrote ", written.word12ByKindCsvPath.string());
    emit(context, spice::mix::EventLevel::Info, "  - Wrote ", written.embeddedMldSummaryCsvPath.string());
    emit(context, spice::mix::EventLevel::Progress,
        "Step 4/4: Finalizing summary.");
    emit(context, spice::mix::EventLevel::Info, "Operation finished.");
    emit(context, spice::mix::EventLevel::Info, "FilesProcessed=", corpus.files.size());
    emit(context, spice::mix::EventLevel::Info, "input=", request.input.string());
    emit(context, spice::mix::EventLevel::Info, "outputDir=", request.output.string());
    return 0;
}
int executeMlkBlenderIr(
    const spice::mix::ExportMlkBlenderIrRequest& request,
    spice::mix::OperationContext& context) {
    std::filesystem::create_directories(request.output);
    emit(context, spice::mix::EventLevel::Progress,
        "Step 2/4: Exporting MLK Blender IR contact sheets.");
    spice::mlk::MlkBlenderIrExportOptions exportOptions{};
    exportOptions.annotationRepositoryDir = request.annotationRepository;
    exportOptions.overwriteMlkAnnotations = request.overwriteAnnotations;
    const auto exported = spice::mlk::exportMlkBlenderIr(request.input, request.output, exportOptions);
    emit(context, spice::mix::EventLevel::Progress,
        "Step 3/4: Writing MLK Blender IR artifacts.");
    for (const auto& file : exported.files) {
        emit(context, spice::mix::EventLevel::Info, "  - Wrote ", file.combinedBlenderIrPath.string());
        emit(context, spice::mix::EventLevel::Info, "  - Wrote ", file.manifestPath.string());
        emit(context, spice::mix::EventLevel::Info, "  - Wrote ", file.recordsCsvPath.string());
        if (file.wroteAnnotation) {
            emit(context, spice::mix::EventLevel::Info, "  - Wrote ", file.annotationPath.string());
        } else if (file.preservedExistingAnnotation) {
            emit(context, spice::mix::EventLevel::Info,
                "  - Preserved existing MLK annotation ", file.annotationPath.string());
        }
        if (file.copiedAnnotationCombinedBlenderIr) {
            emit(context, spice::mix::EventLevel::Info,
                "  - Copied annotation Blender IR ", file.annotationCombinedBlenderIrPath.string());
        }
    }
    emit(context, spice::mix::EventLevel::Progress,
        "Step 4/4: Finalizing summary.");
    emit(context, spice::mix::EventLevel::Info, "Operation finished.");
    emit(context, spice::mix::EventLevel::Info, "FilesProcessed=", exported.files.size());
    emit(context, spice::mix::EventLevel::Info, "input=", request.input.string());
    emit(context, spice::mix::EventLevel::Info, "outputDir=", request.output.string());
    return 0;
}
