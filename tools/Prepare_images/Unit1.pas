unit Unit1;

interface

uses
  Winapi.Windows, Winapi.Messages, System.SysUtils, System.Variants, System.Classes, Vcl.Graphics,
  Vcl.Controls, Vcl.Forms, Vcl.Dialogs, Vcl.StdCtrls, Vcl.ExtCtrls, Vcl.ExtDlgs;

type
  TForm1 = class(TForm)
    btnOpen: TButton;
    btnSave: TButton;
    lbl1: TLabel;
    dlgOpenPic1: TOpenPictureDialog;
    pnl1: TPanel;
    img1: TImage;
    lblSize: TLabel;
    lbl2: TLabel;
    txt1: TStaticText;
    edName: TEdit;
    lbl3: TLabel;
    lblSaved: TLabel;
    procedure btnOpenClick(Sender: TObject);
    procedure btnSaveClick(Sender: TObject);
  private
    { Private declarations }
  public
    { Public declarations }
  end;

var
  Form1: TForm1;

implementation

{$R *.dfm}

procedure TForm1.btnOpenClick(Sender: TObject);
var
  fn : string;
  W, H : Integer;
  i, numfiles : Integer;
begin
  if not dlgOpenPic1.Execute then exit;
  numfiles := dlgOpenPic1.Files.Count;
  for i := 0 to numfiles-1 do
    begin
    fn := dlgOpenPic1.Files[i];
    edName.Text := ChangeFileExt(ExtractFileName(fn), '');
    img1.Picture.LoadFromFile(fn);
    W := img1.Picture.Width;
    H := img1.Picture.Height;
    lblSize.Caption := 'Width  : ' + IntToStr(W) + #13#10 +
                       'Height : ' + IntToStr(H);
    btnSave.Enabled := True;
    lblSaved.Caption := '';

    Application.ProcessMessages;
    // auto save (batch mode)
    if numfiles > 1 then btnSaveClick(nil);
    end;
end;

procedure TForm1.btnSaveClick(Sender: TObject);
var
  fn : string;
  FajlBin_MS: TMemoryStream;
  Header: array[0..5] of byte;
  W, H : Integer;
  Row, Col : integer;
  InPixel : UInt32; // 24 bit color
  R, G, B : byte;
  OutPixel : Uint16;
begin
  fn := ExtractFilePath(dlgOpenPic1.FileName) + edName.Text + '.clk';

  W := img1.Picture.Width;
  H := img1.Picture.Height;

  FajlBin_MS := TMemoryStream.Create;
  Header[0] := byte('C');
  Header[1] := byte('K');
  // Width 16-bit, LSB first
  Header[3] := (W SHR 8) AND $FF;
  Header[2] :=  W        AND $FF;
  // Height 16-bit, LSB first
  Header[5] := (H SHR 8) AND $FF;
  Header[4] :=  H        AND $FF;
  FajlBin_MS.Write (Header[0], 6);

  for Row := 0 to H-1 do
    for Col := 0 to W-1 do
    begin
    InPixel := img1.Canvas.Pixels[Col, Row];  // COLORREF byte order x00bbggrr
    R :=  InPixel         AND $FF;
    G := (InPixel SHR 8)  AND $FF;
    B := (InPixel SHR 16) AND $FF;
                 // 5 bytes              6 bytes              5 bytes
    OutPixel := ((R AND $F8) SHL 8) OR ((G AND $FC) SHL 3) OR (B SHR 3);
    FajlBin_MS.Write (OutPixel, 2);
    end;

  FajlBin_MS.SaveToFile(fn);
  FajlBin_MS.Free;
  lblSaved.Caption := 'Saved.';
end;

end.
